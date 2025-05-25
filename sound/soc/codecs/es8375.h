/* SPDX-License-Identifier: GPL-2.0-only */
/*
* ES8375.h  --  ES8375 ALSA SoC Audio Codec
*
* Authors:
*
* Based on ES8375.h by Michael Zhang
*/
#ifndef _ES8375_H
#define _ES8375_H

// Registors
#define ES8375_RESET1              0x00
#define ES8375_MCLK_SEL            0x01
#define ES8375_CLK_MGR2            0x02
#define ES8375_CLK_MGR3            0x03
#define ES8375_CLK_MGR4            0x04
#define ES8375_CLK_MGR5            0x05
#define ES8375_CLK_MGR6            0x06
#define ES8375_CLK_MGR7            0x07
#define ES8375_CLK_MGR8            0x08
#define ES8375_CLK_MGR9            0x09
#define ES8375_CLK_MGR10           0x0A
#define ES8375_CLK_MGR11           0x0B
#define ES8375_CLK_MGR12           0x0C
#define ES8375_DIV_SPKCLK          0x0E
#define ES8375_CSM1                0x0F
#define ES8375_CSM2                0x10
#define ES8375_VMID_CHARGE2        0x11
#define ES8375_VMID_CHARGE3        0x12
#define ES8375_SDP                 0x15
#define ES8375_SDP2                0x16
#define ES8375_ADC1                0x17
#define ES8375_ADC2                0x18
#define ES8375_ADC_OSR_GAIN        0x19
#define ES8375_ADC_VOLUME          0x1A
#define ES8375_ADC_AUTOMUTE        0x1B
#define ES8375_ADC_AUTOMUTE_ATTN   0x1C
#define ES8375_HPF1                0x1D
#define ES8375_DAC1                0x1F
#define ES8375_DAC2                0x20
#define ES8375_DAC_VOLUME          0x21
#define ES8375_DAC_VPPSCALE        0x22
#define ES8375_DAC_AUTOMUTE1       0x23
#define ES8375_DAC_AUTOMUTE        0x24
#define ES8375_DAC_CAL             0x25
#define ES8375_DAC_OTP             0x27
#define ES8375_ANALOG_SPK1         0x28
#define ES8375_ANALOG_SPK2         0x29
#define ES8375_VMID_SEL            0x2D
#define ES8375_ANALOG1             0x2E
#define ES8375_ANALOG2             0x32
#define ES8375_ANALOG3             0x37
#define ES8375_ADC2DAC_CLKTRI      0xF8
#define ES8375_SYS_CTRL2           0xF9
#define ES8375_FLAGS2              0xFB
#define ES8375_SPK_OFFSET          0xFC
#define ES8375_CHIP_ID1            0xFD
#define ES8375_CHIP_ID0            0xFE
#define ES8375_CHIP_VERSION        0xFF

// Bit Shifts
#define ADC_OSR_GAIN_SHIFT_0        0
#define ADC_RAMPRATE_SHIFT_0        0
#define ADC_VOLUME_SHIFT_0          0
#define ADC_AUTOMUTE_NG_SHIFT_0     0
#define ADC_AUTOMUTE_ATTN_SHIFT_0   0
#define DAC_RAMPRATE_SHIFT_0        0
#define DAC_VOLUME_SHIFT_0          0
#define DAC_VPPSCALE_SHIFT_0        0
#define DAC_AUTOMUTE_NG_SHIFT_0     0
#define DAC_AUTOMUTE_ATTN_SHIFT_0   0
#define DMIC_GAIN_SHIFT_2           2
#define ADC_AUTOMUTE_WS_SHIFT_3     3
#define DMIC_POL_SHIFT_4            4
#define DAC_RAMCLR_SHIFT_4          4
#define ES8375_EN_MODL_SHIFT_4      4
#define ADC_RAMCLR_SHIFT_5          5
#define ADC_HPF_SHIFT_5             5
#define DAC_INV_SHIFT_5             5
#define DAC_AUTOMUTE_WS_SHIFT_5     5
#define ES8375_EN_PGAL_SHIFT_5      5
#define ES8375_ADC_P2S_MUTE_SHIFT_5 5
#define ADC_INV_SHIFT_6             6
#define DAC_DEMMUTE_SHIFT_6         6
#define ES8375_DAC_S2P_MUTE_SHIFT_6 6
#define ADC_SRC_SHIFT_7             7
#define ADC_AUTOMUTE_SHIFT_7        7
#define DAC_DSMMUTE_SHIFT_7         7
#define DAC_AUTOMUTE_EN_SHIFT_7     7

// Function values
#define ES8375_ADC_OSR_GAIN_MAX         0x3F
#define ES8375_DMIC_GAIN_MAX            0x04
#define ES8375_ADC_AUTOMUTE_ATTN_MAX    0x1F
#define ES8375_AUTOMUTE_NG_MAX          0x07
#define ES8375_ADC_VOLUME_MAX           0xFF
#define ES8375_DAC_VOLUME_MAX           0xFF
#define ES8375_DAC_VPPSCALE_MAX         0x3F
#define ES8375_DAC_AUTOMUTE_ATTN_MAX    0x17
#define ES8375_REG_MAX                  0xFF

enum ES8375_supplies {
	ES8375_SUPPLY_VD = 0,
	ES8375_SUPPLY_VA,
};

// Properties
#define ES8375_3V3  1
#define ES8375_1V8  0

#define ES8375_MCLK_PIN	0
#define ES8375_BCLK_PIN 1
#define ES8375_MCLK_SOURCE	ES8375_MCLK_PIN

#define DMIC_POSITIVE_EDGE  0
#define DMIC_NEGATIVE_EDGE  1
#define DMIC_POL  DMIC_POSITIVE_EDGE

#define PA_SHUTDOWN     0
#define PA_ENABLE       1

#endif
