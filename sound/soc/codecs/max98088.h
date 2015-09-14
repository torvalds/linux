/*
 * max98088.h -- MAX98088 ALSA SoC Audio driver
 *
 * Copyright 2010 Maxim Integrated Products
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MAX98088_H
#define _MAX98088_H

/*
 * MAX98088 Registers Definition
 */
#define M98088_REG_00_IRQ_STATUS            0x00
#define M98088_REG_01_MIC_STATUS            0x01
#define M98088_REG_02_JACK_STATUS           0x02
#define M98088_REG_03_BATTERY_VOLTAGE       0x03
#define M98088_REG_0F_IRQ_ENABLE            0x0F
#define M98088_REG_10_SYS_CLK               0x10
#define M98088_REG_11_DAI1_CLKMODE          0x11
#define M98088_REG_12_DAI1_CLKCFG_HI        0x12
#define M98088_REG_13_DAI1_CLKCFG_LO        0x13
#define M98088_REG_14_DAI1_FORMAT           0x14
#define M98088_REG_15_DAI1_CLOCK            0x15
#define M98088_REG_16_DAI1_IOCFG            0x16
#define M98088_REG_17_DAI1_TDM              0x17
#define M98088_REG_18_DAI1_FILTERS          0x18
#define M98088_REG_19_DAI2_CLKMODE          0x19
#define M98088_REG_1A_DAI2_CLKCFG_HI        0x1A
#define M98088_REG_1B_DAI2_CLKCFG_LO        0x1B
#define M98088_REG_1C_DAI2_FORMAT           0x1C
#define M98088_REG_1D_DAI2_CLOCK            0x1D
#define M98088_REG_1E_DAI2_IOCFG            0x1E
#define M98088_REG_1F_DAI2_TDM              0x1F
#define M98088_REG_20_DAI2_FILTERS          0x20
#define M98088_REG_21_SRC                   0x21
#define M98088_REG_22_MIX_DAC               0x22
#define M98088_REG_23_MIX_ADC_LEFT          0x23
#define M98088_REG_24_MIX_ADC_RIGHT         0x24
#define M98088_REG_25_MIX_HP_LEFT           0x25
#define M98088_REG_26_MIX_HP_RIGHT          0x26
#define M98088_REG_27_MIX_HP_CNTL           0x27
#define M98088_REG_28_MIX_REC_LEFT          0x28
#define M98088_REG_29_MIX_REC_RIGHT         0x29
#define M98088_REG_2A_MIC_REC_CNTL          0x2A
#define M98088_REG_2B_MIX_SPK_LEFT          0x2B
#define M98088_REG_2C_MIX_SPK_RIGHT         0x2C
#define M98088_REG_2D_MIX_SPK_CNTL          0x2D
#define M98088_REG_2E_LVL_SIDETONE          0x2E
#define M98088_REG_2F_LVL_DAI1_PLAY         0x2F
#define M98088_REG_30_LVL_DAI1_PLAY_EQ      0x30
#define M98088_REG_31_LVL_DAI2_PLAY         0x31
#define M98088_REG_32_LVL_DAI2_PLAY_EQ      0x32
#define M98088_REG_33_LVL_ADC_L             0x33
#define M98088_REG_34_LVL_ADC_R             0x34
#define M98088_REG_35_LVL_MIC1              0x35
#define M98088_REG_36_LVL_MIC2              0x36
#define M98088_REG_37_LVL_INA               0x37
#define M98088_REG_38_LVL_INB               0x38
#define M98088_REG_39_LVL_HP_L              0x39
#define M98088_REG_3A_LVL_HP_R              0x3A
#define M98088_REG_3B_LVL_REC_L             0x3B
#define M98088_REG_3C_LVL_REC_R             0x3C
#define M98088_REG_3D_LVL_SPK_L             0x3D
#define M98088_REG_3E_LVL_SPK_R             0x3E
#define M98088_REG_3F_MICAGC_CFG            0x3F
#define M98088_REG_40_MICAGC_THRESH         0x40
#define M98088_REG_41_SPKDHP                0x41
#define M98088_REG_42_SPKDHP_THRESH         0x42
#define M98088_REG_43_SPKALC_COMP           0x43
#define M98088_REG_44_PWRLMT_CFG            0x44
#define M98088_REG_45_PWRLMT_TIME           0x45
#define M98088_REG_46_THDLMT_CFG            0x46
#define M98088_REG_47_CFG_AUDIO_IN          0x47
#define M98088_REG_48_CFG_MIC               0x48
#define M98088_REG_49_CFG_LEVEL             0x49
#define M98088_REG_4A_CFG_BYPASS            0x4A
#define M98088_REG_4B_CFG_JACKDET           0x4B
#define M98088_REG_4C_PWR_EN_IN             0x4C
#define M98088_REG_4D_PWR_EN_OUT            0x4D
#define M98088_REG_4E_BIAS_CNTL             0x4E
#define M98088_REG_4F_DAC_BIAS1             0x4F
#define M98088_REG_50_DAC_BIAS2             0x50
#define M98088_REG_51_PWR_SYS               0x51
#define M98088_REG_52_DAI1_EQ_BASE          0x52
#define M98088_REG_84_DAI2_EQ_BASE          0x84
#define M98088_REG_B6_DAI1_BIQUAD_BASE      0xB6
#define M98088_REG_C0_DAI2_BIQUAD_BASE      0xC0
#define M98088_REG_FF_REV_ID                0xFF

#define M98088_REG_CNT                      (0xFF+1)

/* MAX98088 Registers Bit Fields */

/* M98088_REG_11_DAI1_CLKMODE, M98088_REG_19_DAI2_CLKMODE */
       #define M98088_CLKMODE_MASK             0xFF

/* M98088_REG_14_DAI1_FORMAT, M98088_REG_1C_DAI2_FORMAT */
       #define M98088_DAI_MAS                  (1<<7)
       #define M98088_DAI_WCI                  (1<<6)
       #define M98088_DAI_BCI                  (1<<5)
       #define M98088_DAI_DLY                  (1<<4)
       #define M98088_DAI_TDM                  (1<<2)
       #define M98088_DAI_FSW                  (1<<1)
       #define M98088_DAI_WS                   (1<<0)

/* M98088_REG_15_DAI1_CLOCK, M98088_REG_1D_DAI2_CLOCK */
       #define M98088_DAI_BSEL64               (1<<0)
       #define M98088_DAI_OSR64                (1<<6)

/* M98088_REG_16_DAI1_IOCFG, M98088_REG_1E_DAI2_IOCFG */
       #define M98088_S1NORMAL                 (1<<6)
       #define M98088_S2NORMAL                 (2<<6)
       #define M98088_SDATA                    (3<<0)

/* M98088_REG_18_DAI1_FILTERS, M98088_REG_20_DAI2_FILTERS */
       #define M98088_DAI_DHF                  (1<<3)

/* M98088_REG_22_MIX_DAC */
       #define M98088_DAI1L_TO_DACL            (1<<7)
       #define M98088_DAI1R_TO_DACL            (1<<6)
       #define M98088_DAI2L_TO_DACL            (1<<5)
       #define M98088_DAI2R_TO_DACL            (1<<4)
       #define M98088_DAI1L_TO_DACR            (1<<3)
       #define M98088_DAI1R_TO_DACR            (1<<2)
       #define M98088_DAI2L_TO_DACR            (1<<1)
       #define M98088_DAI2R_TO_DACR            (1<<0)

/* M98088_REG_2A_MIC_REC_CNTL */
       #define M98088_REC_LINEMODE             (1<<7)
       #define M98088_REC_LINEMODE_MASK        (1<<7)

/* M98088_REG_2D_MIX_SPK_CNTL */
       #define M98088_MIX_SPKR_GAIN_MASK       (3<<2)
       #define M98088_MIX_SPKR_GAIN_SHIFT      2
       #define M98088_MIX_SPKL_GAIN_MASK       (3<<0)
       #define M98088_MIX_SPKL_GAIN_SHIFT      0

/* M98088_REG_2F_LVL_DAI1_PLAY, M98088_REG_31_LVL_DAI2_PLAY */
       #define M98088_DAI_MUTE                 (1<<7)
       #define M98088_DAI_MUTE_MASK            (1<<7)
       #define M98088_DAI_VOICE_GAIN_MASK      (3<<4)
       #define M98088_DAI_ATTENUATION_MASK     (0xF<<0)
       #define M98088_DAI_ATTENUATION_SHIFT    0

/* M98088_REG_35_LVL_MIC1, M98088_REG_36_LVL_MIC2 */
       #define M98088_MICPRE_MASK              (3<<5)
       #define M98088_MICPRE_SHIFT             5

/* M98088_REG_3A_LVL_HP_R */
       #define M98088_HP_MUTE                  (1<<7)

/* M98088_REG_3C_LVL_REC_R */
       #define M98088_REC_MUTE                 (1<<7)

/* M98088_REG_3E_LVL_SPK_R */
       #define M98088_SP_MUTE                  (1<<7)

/* M98088_REG_48_CFG_MIC */
       #define M98088_EXTMIC_MASK              (3<<0)
       #define M98088_DIGMIC_L                 (1<<5)
       #define M98088_DIGMIC_R                 (1<<4)

/* M98088_REG_49_CFG_LEVEL */
       #define M98088_VSEN                     (1<<6)
       #define M98088_ZDEN                     (1<<5)
       #define M98088_EQ2EN                    (1<<1)
       #define M98088_EQ1EN                    (1<<0)

/* M98088_REG_4C_PWR_EN_IN */
       #define M98088_INAEN                    (1<<7)
       #define M98088_INBEN                    (1<<6)
       #define M98088_MBEN                     (1<<3)
       #define M98088_ADLEN                    (1<<1)
       #define M98088_ADREN                    (1<<0)

/* M98088_REG_4D_PWR_EN_OUT */
       #define M98088_HPLEN                    (1<<7)
       #define M98088_HPREN                    (1<<6)
       #define M98088_HPEN                     ((1<<7)|(1<<6))
       #define M98088_SPLEN                    (1<<5)
       #define M98088_SPREN                    (1<<4)
       #define M98088_RECEN                    (1<<3)
       #define M98088_DALEN                    (1<<1)
       #define M98088_DAREN                    (1<<0)

/* M98088_REG_51_PWR_SYS */
       #define M98088_SHDNRUN                  (1<<7)
       #define M98088_PERFMODE                 (1<<3)
       #define M98088_HPPLYBACK                (1<<2)
       #define M98088_PWRSV8K                  (1<<1)
       #define M98088_PWRSV                    (1<<0)

/* Line inputs */
#define LINE_INA  0
#define LINE_INB  1

#define M98088_COEFS_PER_BAND               5

#define M98088_BYTE1(w) ((w >> 8) & 0xff)
#define M98088_BYTE0(w) (w & 0xff)

#endif
