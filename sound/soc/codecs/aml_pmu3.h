/*
 * aml_pmu3.h  --  AML PMU3 Soc Audio codec driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _AML_PMU3_H
#define _AML_PMU3_H

#define PMU3_ADDR				0x35
#define PMU3_AUDIO_BASE			0x220

#define PMU3_SOFTWARE_RESET		0x0
#define PMU3_BLOCK_ENABLE_1		0x1
#define PMU3_BLOCK_ENABLE_2		0x2
#define PMU3_PGA_IN				0x3
#define PMU3_MIXIN_L			0x4
#define PMU3_MIXIN_R			0x5
#define PMU3_MIXOUT_L			0x6
#define PMU3_MIXOUT_R			0x7
#define PMU3_RXV_TO_MIXOUT		0x8
#define PMU3_LINEOUT_HP_DRV		0x9
#define PMU3_HP_DC_OFFSET		0xA
#define PMU3_ADC_DAC			0xB
#define PMU3_HP_MIC_DET			0xC
#define PMU3_ADC_VOLUME_CTL		0xD
#define PMU3_DAC_VOLUME_CTL		0xE
#define PMU3_SOFT_MUTE			0xF
#define PMU3_SIDETONE_MIXING	0x10
#define PMU3_DMIC_GPIO			0x11
#define PMU3_MONITOR_REG		0x12

/* R0Ch PMU3_HP_MIC_DET */
#define PMU3_MIC_BIAS1_SHIFT		15
#define PMU3_MIC_BIAS2_SHIFT		14

/* R0Bh PMU3_ADC_DAC */
#define PMU3_ADC_HPF_MODE_SHIFT		10

/* R0FH PMU3_SOFT_MUTE	*/
#define PMU3_DAC_RAMP_RATE_SHIFT		11

/* R10h PMU3_SIDETONE_MIXING */
#define PMU3_DACL_ST_SRC_SHIFT		7
#define PMU3_DACR_ST_SRC_SHIFT		6
#define PMU3_ADCDATL_SRC_SHIFT		4
#define PMU3_ADCDATR_SRC_SHIFT		3
#define PMU3_DACDATL_SRC_SHIFT		2
#define PMU3_DACDATR_SRC_SHIFT		1

#endif
