/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCM3168A codec driver header
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 *
 * Author: Damien Horsley <Damien.Horsley@imgtec.com>
 */

#ifndef __PCM3168A_H__
#define __PCM3168A_H__

extern const struct dev_pm_ops pcm3168a_pm_ops;
extern const struct regmap_config pcm3168a_regmap;

extern int pcm3168a_probe(struct device *dev, struct regmap *regmap);
extern void pcm3168a_remove(struct device *dev);

#define PCM3168A_RST_SMODE			0x40
#define PCM3168A_MRST_MASK			0x80
#define PCM3168A_SRST_MASK			0x40
#define PCM3168A_DAC_SRDA_SHIFT			0
#define PCM3168A_DAC_SRDA_MASK			0x3

#define PCM3168A_DAC_PWR_MST_FMT		0x41
#define PCM3168A_DAC_PSMDA_SHIFT		7
#define PCM3168A_DAC_PSMDA_MASK			0x80
#define PCM3168A_DAC_MSDA_SHIFT			4
#define PCM3168A_DAC_MSDA_MASK			0x70
#define PCM3168A_DAC_FMT_SHIFT			0
#define PCM3168A_DAC_FMT_MASK			0xf

#define PCM3168A_DAC_OP_FLT			0x42
#define PCM3168A_DAC_OPEDA_SHIFT		4
#define PCM3168A_DAC_OPEDA_MASK			0xf0
#define PCM3168A_DAC_FLT_SHIFT			0
#define PCM3168A_DAC_FLT_MASK			0xf

#define PCM3168A_DAC_INV			0x43

#define PCM3168A_DAC_MUTE			0x44

#define PCM3168A_DAC_ZERO			0x45

#define PCM3168A_DAC_ATT_DEMP_ZF		0x46
#define PCM3168A_DAC_ATMDDA_MASK		0x80
#define PCM3168A_DAC_ATMDDA_SHIFT		7
#define PCM3168A_DAC_ATSPDA_MASK		0x40
#define PCM3168A_DAC_ATSPDA_SHIFT		6
#define PCM3168A_DAC_DEMP_SHIFT			4
#define PCM3168A_DAC_DEMP_MASK			0x30
#define PCM3168A_DAC_AZRO_SHIFT			1
#define PCM3168A_DAC_AZRO_MASK			0xe
#define PCM3168A_DAC_ZREV_MASK			0x1
#define PCM3168A_DAC_ZREV_SHIFT			0

#define PCM3168A_DAC_VOL_MASTER			0x47

#define PCM3168A_DAC_VOL_CHAN_START		0x48

#define PCM3168A_ADC_SMODE			0x50
#define PCM3168A_ADC_SRAD_SHIFT			0
#define PCM3168A_ADC_SRAD_MASK			0x3

#define PCM3168A_ADC_MST_FMT			0x51
#define PCM3168A_ADC_MSAD_SHIFT			4
#define PCM3168A_ADC_MSAD_MASK			0x70
#define PCM3168A_ADC_FMTAD_SHIFT		0
#define PCM3168A_ADC_FMTAD_MASK			0x7

#define PCM3168A_ADC_PWR_HPFB			0x52
#define PCM3168A_ADC_PSVAD_SHIFT		4
#define PCM3168A_ADC_PSVAD_MASK			0x70
#define PCM3168A_ADC_BYP_SHIFT			0
#define PCM3168A_ADC_BYP_MASK			0x7

#define PCM3168A_ADC_SEAD			0x53

#define PCM3168A_ADC_INV			0x54

#define PCM3168A_ADC_MUTE			0x55

#define PCM3168A_ADC_OV				0x56

#define PCM3168A_ADC_ATT_OVF			0x57
#define PCM3168A_ADC_ATMDAD_MASK		0x80
#define PCM3168A_ADC_ATMDAD_SHIFT		7
#define PCM3168A_ADC_ATSPAD_MASK		0x40
#define PCM3168A_ADC_ATSPAD_SHIFT		6
#define PCM3168A_ADC_OVFP_MASK			0x1
#define PCM3168A_ADC_OVFP_SHIFT			0

#define PCM3168A_ADC_VOL_MASTER			0x58

#define PCM3168A_ADC_VOL_CHAN_START		0x59

#endif
