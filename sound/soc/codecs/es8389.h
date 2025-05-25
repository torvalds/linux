/* SPDX-License-Identifier: GPL-2.0-only */
/*
* ES8389.h  --  ES8389 ALSA SoC Audio Codec
*
* Authors:
*
* Based on ES8374.h by Michael Zhang
*/

#ifndef _ES8389_H
#define _ES8389_H

/*
*     ES8389_REGISTER NAME_REG_REGISTER ADDRESS
*/
#define ES8389_RESET                  0x00  /*reset digital,csm,clock manager etc.*/

/*
* Clock Scheme Register definition
*/
#define ES8389_MASTER_MODE            0x01
#define ES8389_MASTER_CLK             0x02
#define ES8389_CLK_OFF1               0x03
#define ES8389_CLK_DIV1               0x04
#define ES8389_CLK_MUL                0x05
#define ES8389_CLK_MUX1               0x06
#define ES8389_CLK_MUX2               0x07
#define ES8389_CLK_CTL1               0x08
#define ES8389_CLK_CTL2               0x09
#define ES8389_CLK_CTL3               0x0A
#define ES8389_SCLK_DIV               0x0B
#define ES8389_LRCK_DIV1              0x0C
#define ES8389_LRCK_DIV2              0x0D
#define ES8389_CLK_OFF2               0x0E
#define ES8389_OSC_CLK                0x0F
#define ES8389_CSM_JUMP               0x10
#define ES8389_CLK_DIV2               0x11
#define ES8389_SYSTEM12               0x12
#define ES8389_SYSTEM13               0x13
#define ES8389_SYSTEM14               0x14
#define ES8389_SYSTEM15               0x15
#define ES8389_SYSTEM16               0x16
#define ES8389_SYSTEM17               0x17
#define ES8389_SYSTEM18               0x18
#define ES8389_SYSTEM19               0x19
#define ES8389_SYSTEM1A               0x1A
#define ES8389_SYSTEM1B               0x1B
#define ES8389_SYSTEM1C               0x1C
#define ES8389_ADC_FORMAT_MUTE        0x20
#define ES8389_ADC_OSR                0x21
#define ES8389_ADC_DSP                0x22
#define ES8389_ADC_MODE               0x23
#define ES8389_ADC_HPF1               0x24
#define ES8389_ADC_HPF2               0x25
#define ES8389_OSR_VOL                0x26
#define ES8389_ADCL_VOL               0x27
#define ES8389_ADCR_VOL               0x28
#define ES8389_ALC_CTL                0x29
#define ES8389_PTDM_SLOT              0x2A
#define ES8389_ALC_ON                 0x2B
#define ES8389_ALC_TARGET             0x2C
#define ES8389_ALC_GAIN               0x2D
#define ES8389_SYSTEM2E               0x2E
#define ES8389_ADC_MUTE               0x2F
#define ES8389_SYSTEM30               0x30
#define ES8389_ADC_RESET              0x31
#define ES8389_DAC_FORMAT_MUTE        0x40
#define ES8389_DAC_DSM_OSR            0x41
#define ES8389_DAC_DSP_OSR            0x42
#define ES8389_DAC_MISC               0x43
#define ES8389_DAC_MIX                0x44
#define ES8389_DAC_INV                0x45
#define ES8389_DACL_VOL               0x46
#define ES8389_DACR_VOL               0x47
#define ES8389_MIX_VOL                0x48
#define ES8389_DAC_RAMP               0x49
#define ES8389_SYSTEM4C               0x4C
#define ES8389_DAC_RESET              0x4D
#define ES8389_VMID                   0x60
#define ES8389_ANA_CTL1               0x61
#define ES8389_ANA_VSEL               0x62
#define ES8389_ANA_CTL2               0x63
#define ES8389_ADC_EN                 0x64
#define ES8389_HPSW                   0x69
#define ES8389_LOW_POWER1             0x6B
#define ES8389_LOW_POWER2             0x6C
#define ES8389_DMIC_EN                0x6D
#define ES8389_PGA_SW                 0x6E
#define ES8389_MOD_SW1                0x6F
#define ES8389_MOD_SW2                0x70
#define ES8389_MOD_SW3                0x71
#define ES8389_MIC1_GAIN              0x72
#define ES8389_MIC2_GAIN              0x73

#define ES8389_CHIP_MISC              0xF0
#define ES8389_CSM_STATE1             0xF1
#define ES8389_PULL_DOWN              0xF2
#define ES8389_ISO_CTL                0xF3
#define ES8389_CSM_STATE2             0xF4

#define ES8389_CHIP_ID0               0xFD
#define ES8389_CHIP_ID1               0xFE

#define ES8389_MAX_REGISTER           0xFF

#define ES8389_MIC_SEL_MASK           (7 << 4)
#define ES8389_MIC_DEFAULT            (1 << 4)

#define ES8389_MASTER_MODE_EN         (1 << 0)

#define ES8389_TDM_OFF                (0 << 0)
#define ES8389_STDM_ON                (1 << 7)
#define ES8389_PTDM_ON                (1 << 6)

#define ES8389_TDM_MODE               ES8389_TDM_OFF
#define ES8389_TDM_SLOT               (0x70 << 0)
#define ES8389_TDM_SHIFT              4

#define ES8389_MCLK_SOURCE            (1 << 6)
#define ES8389_MCLK_PIN               (1 << 6)
#define ES8389_SCLK_PIN               (0 << 6)

/* ES8389_FMT */
#define ES8389_S24_LE                 (0 << 5)
#define ES8389_S20_3_LE               (1 << 5)
#define ES8389_S18_LE                 (2 << 5)
#define ES8389_S16_LE                 (3 << 5)
#define ES8389_S32_LE                 (4 << 5)
#define ES8389_DATA_LEN_MASK          (7 << 5)

#define ES8389_DAIFMT_MASK            (7 << 2)
#define ES8389_DAIFMT_I2S             0
#define ES8389_DAIFMT_LEFT_J          (1 << 2)
#define ES8389_DAIFMT_DSP_A           (1 << 3)
#define ES8389_DAIFMT_DSP_B           (3 << 3)

#define ES8389_STATE_ON               (13 << 0)
#define ES8389_STATE_STANDBY          (7 << 0)

#endif
