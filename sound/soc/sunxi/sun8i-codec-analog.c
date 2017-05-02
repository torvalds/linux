/*
 * This driver supports the analog controls for the internal codec
 * found in Allwinner's A31s, A23, A33 and H3 SoCs.
 *
 * Copyright 2016 Chen-Yu Tsai <wens@csie.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

/* Codec analog control register offsets and bit fields */
#define SUN8I_ADDA_HP_VOLC		0x00
#define SUN8I_ADDA_HP_VOLC_PA_CLK_GATE		7
#define SUN8I_ADDA_HP_VOLC_HP_VOL		0
#define SUN8I_ADDA_LOMIXSC		0x01
#define SUN8I_ADDA_LOMIXSC_MIC1			6
#define SUN8I_ADDA_LOMIXSC_MIC2			5
#define SUN8I_ADDA_LOMIXSC_PHONE		4
#define SUN8I_ADDA_LOMIXSC_PHONEN		3
#define SUN8I_ADDA_LOMIXSC_LINEINL		2
#define SUN8I_ADDA_LOMIXSC_DACL			1
#define SUN8I_ADDA_LOMIXSC_DACR			0
#define SUN8I_ADDA_ROMIXSC		0x02
#define SUN8I_ADDA_ROMIXSC_MIC1			6
#define SUN8I_ADDA_ROMIXSC_MIC2			5
#define SUN8I_ADDA_ROMIXSC_PHONE		4
#define SUN8I_ADDA_ROMIXSC_PHONEP		3
#define SUN8I_ADDA_ROMIXSC_LINEINR		2
#define SUN8I_ADDA_ROMIXSC_DACR			1
#define SUN8I_ADDA_ROMIXSC_DACL			0
#define SUN8I_ADDA_DAC_PA_SRC		0x03
#define SUN8I_ADDA_DAC_PA_SRC_DACAREN		7
#define SUN8I_ADDA_DAC_PA_SRC_DACALEN		6
#define SUN8I_ADDA_DAC_PA_SRC_RMIXEN		5
#define SUN8I_ADDA_DAC_PA_SRC_LMIXEN		4
#define SUN8I_ADDA_DAC_PA_SRC_RHPPAMUTE		3
#define SUN8I_ADDA_DAC_PA_SRC_LHPPAMUTE		2
#define SUN8I_ADDA_DAC_PA_SRC_RHPIS		1
#define SUN8I_ADDA_DAC_PA_SRC_LHPIS		0
#define SUN8I_ADDA_PHONEIN_GCTRL	0x04
#define SUN8I_ADDA_PHONEIN_GCTRL_PHONEPG	4
#define SUN8I_ADDA_PHONEIN_GCTRL_PHONENG	0
#define SUN8I_ADDA_LINEIN_GCTRL		0x05
#define SUN8I_ADDA_LINEIN_GCTRL_LINEING		4
#define SUN8I_ADDA_LINEIN_GCTRL_PHONEG		0
#define SUN8I_ADDA_MICIN_GCTRL		0x06
#define SUN8I_ADDA_MICIN_GCTRL_MIC1G		4
#define SUN8I_ADDA_MICIN_GCTRL_MIC2G		0
#define SUN8I_ADDA_PAEN_HP_CTRL		0x07
#define SUN8I_ADDA_PAEN_HP_CTRL_HPPAEN		7
#define SUN8I_ADDA_PAEN_HP_CTRL_LINEOUTEN	7	/* H3 specific */
#define SUN8I_ADDA_PAEN_HP_CTRL_HPCOM_FC	5
#define SUN8I_ADDA_PAEN_HP_CTRL_COMPTEN		4
#define SUN8I_ADDA_PAEN_HP_CTRL_PA_ANTI_POP_CTRL	2
#define SUN8I_ADDA_PAEN_HP_CTRL_LTRNMUTE	1
#define SUN8I_ADDA_PAEN_HP_CTRL_RTLNMUTE	0
#define SUN8I_ADDA_PHONEOUT_CTRL	0x08
#define SUN8I_ADDA_PHONEOUT_CTRL_PHONEOUTG	5
#define SUN8I_ADDA_PHONEOUT_CTRL_PHONEOUTEN	4
#define SUN8I_ADDA_PHONEOUT_CTRL_PHONEOUT_MIC1	3
#define SUN8I_ADDA_PHONEOUT_CTRL_PHONEOUT_MIC2	2
#define SUN8I_ADDA_PHONEOUT_CTRL_PHONEOUT_RMIX	1
#define SUN8I_ADDA_PHONEOUT_CTRL_PHONEOUT_LMIX	0
#define SUN8I_ADDA_PHONE_GAIN_CTRL	0x09
#define SUN8I_ADDA_PHONE_GAIN_CTRL_LINEOUT_VOL	3
#define SUN8I_ADDA_PHONE_GAIN_CTRL_PHONEPREG	0
#define SUN8I_ADDA_MIC2G_CTRL		0x0a
#define SUN8I_ADDA_MIC2G_CTRL_MIC2AMPEN		7
#define SUN8I_ADDA_MIC2G_CTRL_MIC2BOOST		4
#define SUN8I_ADDA_MIC2G_CTRL_LINEOUTLEN	3
#define SUN8I_ADDA_MIC2G_CTRL_LINEOUTREN	2
#define SUN8I_ADDA_MIC2G_CTRL_LINEOUTLSRC	1
#define SUN8I_ADDA_MIC2G_CTRL_LINEOUTRSRC	0
#define SUN8I_ADDA_MIC1G_MICBIAS_CTRL	0x0b
#define SUN8I_ADDA_MIC1G_MICBIAS_CTRL_HMICBIASEN	7
#define SUN8I_ADDA_MIC1G_MICBIAS_CTRL_MMICBIASEN	6
#define SUN8I_ADDA_MIC1G_MICBIAS_CTRL_HMICBIAS_MODE	5
#define SUN8I_ADDA_MIC1G_MICBIAS_CTRL_MIC1AMPEN		3
#define SUN8I_ADDA_MIC1G_MICBIAS_CTRL_MIC1BOOST		0
#define SUN8I_ADDA_LADCMIXSC		0x0c
#define SUN8I_ADDA_LADCMIXSC_MIC1		6
#define SUN8I_ADDA_LADCMIXSC_MIC2		5
#define SUN8I_ADDA_LADCMIXSC_PHONE		4
#define SUN8I_ADDA_LADCMIXSC_PHONEN		3
#define SUN8I_ADDA_LADCMIXSC_LINEINL		2
#define SUN8I_ADDA_LADCMIXSC_OMIXRL		1
#define SUN8I_ADDA_LADCMIXSC_OMIXRR		0
#define SUN8I_ADDA_RADCMIXSC		0x0d
#define SUN8I_ADDA_RADCMIXSC_MIC1		6
#define SUN8I_ADDA_RADCMIXSC_MIC2		5
#define SUN8I_ADDA_RADCMIXSC_PHONE		4
#define SUN8I_ADDA_RADCMIXSC_PHONEP		3
#define SUN8I_ADDA_RADCMIXSC_LINEINR		2
#define SUN8I_ADDA_RADCMIXSC_OMIXR		1
#define SUN8I_ADDA_RADCMIXSC_OMIXL		0
#define SUN8I_ADDA_RES			0x0e
#define SUN8I_ADDA_RES_MMICBIAS_SEL		4
#define SUN8I_ADDA_RES_PA_ANTI_POP_CTRL		0
#define SUN8I_ADDA_ADC_AP_EN		0x0f
#define SUN8I_ADDA_ADC_AP_EN_ADCREN		7
#define SUN8I_ADDA_ADC_AP_EN_ADCLEN		6
#define SUN8I_ADDA_ADC_AP_EN_ADCG		0

/* Analog control register access bits */
#define ADDA_PR			0x0		/* PRCM base + 0x1c0 */
#define ADDA_PR_RESET			BIT(28)
#define ADDA_PR_WRITE			BIT(24)
#define ADDA_PR_ADDR_SHIFT		16
#define ADDA_PR_ADDR_MASK		GENMASK(4, 0)
#define ADDA_PR_DATA_IN_SHIFT		8
#define ADDA_PR_DATA_IN_MASK		GENMASK(7, 0)
#define ADDA_PR_DATA_OUT_SHIFT		0
#define ADDA_PR_DATA_OUT_MASK		GENMASK(7, 0)

/* regmap access bits */
static int adda_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	void __iomem *base = (void __iomem *)context;
	u32 tmp;

	/* De-assert reset */
	writel(readl(base) | ADDA_PR_RESET, base);

	/* Clear write bit */
	writel(readl(base) & ~ADDA_PR_WRITE, base);

	/* Set register address */
	tmp = readl(base);
	tmp &= ~(ADDA_PR_ADDR_MASK << ADDA_PR_ADDR_SHIFT);
	tmp |= (reg & ADDA_PR_ADDR_MASK) << ADDA_PR_ADDR_SHIFT;
	writel(tmp, base);

	/* Read back value */
	*val = readl(base) & ADDA_PR_DATA_OUT_MASK;

	return 0;
}

static int adda_reg_write(void *context, unsigned int reg, unsigned int val)
{
	void __iomem *base = (void __iomem *)context;
	u32 tmp;

	/* De-assert reset */
	writel(readl(base) | ADDA_PR_RESET, base);

	/* Set register address */
	tmp = readl(base);
	tmp &= ~(ADDA_PR_ADDR_MASK << ADDA_PR_ADDR_SHIFT);
	tmp |= (reg & ADDA_PR_ADDR_MASK) << ADDA_PR_ADDR_SHIFT;
	writel(tmp, base);

	/* Set data to write */
	tmp = readl(base);
	tmp &= ~(ADDA_PR_DATA_IN_MASK << ADDA_PR_DATA_IN_SHIFT);
	tmp |= (val & ADDA_PR_DATA_IN_MASK) << ADDA_PR_DATA_IN_SHIFT;
	writel(tmp, base);

	/* Set write bit to signal a write */
	writel(readl(base) | ADDA_PR_WRITE, base);

	/* Clear write bit */
	writel(readl(base) & ~ADDA_PR_WRITE, base);

	return 0;
}

static const struct regmap_config adda_pr_regmap_cfg = {
	.name		= "adda-pr",
	.reg_bits	= 5,
	.reg_stride	= 1,
	.val_bits	= 8,
	.reg_read	= adda_reg_read,
	.reg_write	= adda_reg_write,
	.fast_io	= true,
	.max_register	= 24,
};

/* mixer controls */
static const struct snd_kcontrol_new sun8i_codec_mixer_controls[] = {
	SOC_DAPM_DOUBLE_R("DAC Playback Switch",
			  SUN8I_ADDA_LOMIXSC,
			  SUN8I_ADDA_ROMIXSC,
			  SUN8I_ADDA_LOMIXSC_DACL, 1, 0),
	SOC_DAPM_DOUBLE_R("DAC Reversed Playback Switch",
			  SUN8I_ADDA_LOMIXSC,
			  SUN8I_ADDA_ROMIXSC,
			  SUN8I_ADDA_LOMIXSC_DACR, 1, 0),
	SOC_DAPM_DOUBLE_R("Line In Playback Switch",
			  SUN8I_ADDA_LOMIXSC,
			  SUN8I_ADDA_ROMIXSC,
			  SUN8I_ADDA_LOMIXSC_LINEINL, 1, 0),
	SOC_DAPM_DOUBLE_R("Mic1 Playback Switch",
			  SUN8I_ADDA_LOMIXSC,
			  SUN8I_ADDA_ROMIXSC,
			  SUN8I_ADDA_LOMIXSC_MIC1, 1, 0),
	SOC_DAPM_DOUBLE_R("Mic2 Playback Switch",
			  SUN8I_ADDA_LOMIXSC,
			  SUN8I_ADDA_ROMIXSC,
			  SUN8I_ADDA_LOMIXSC_MIC2, 1, 0),
};

/* ADC mixer controls */
static const struct snd_kcontrol_new sun8i_codec_adc_mixer_controls[] = {
	SOC_DAPM_DOUBLE_R("Mixer Capture Switch",
			  SUN8I_ADDA_LADCMIXSC,
			  SUN8I_ADDA_RADCMIXSC,
			  SUN8I_ADDA_LADCMIXSC_OMIXRL, 1, 0),
	SOC_DAPM_DOUBLE_R("Mixer Reversed Capture Switch",
			  SUN8I_ADDA_LADCMIXSC,
			  SUN8I_ADDA_RADCMIXSC,
			  SUN8I_ADDA_LADCMIXSC_OMIXRR, 1, 0),
	SOC_DAPM_DOUBLE_R("Line In Capture Switch",
			  SUN8I_ADDA_LADCMIXSC,
			  SUN8I_ADDA_RADCMIXSC,
			  SUN8I_ADDA_LADCMIXSC_LINEINL, 1, 0),
	SOC_DAPM_DOUBLE_R("Mic1 Capture Switch",
			  SUN8I_ADDA_LADCMIXSC,
			  SUN8I_ADDA_RADCMIXSC,
			  SUN8I_ADDA_LADCMIXSC_MIC1, 1, 0),
	SOC_DAPM_DOUBLE_R("Mic2 Capture Switch",
			  SUN8I_ADDA_LADCMIXSC,
			  SUN8I_ADDA_RADCMIXSC,
			  SUN8I_ADDA_LADCMIXSC_MIC2, 1, 0),
};

/* volume / mute controls */
static const DECLARE_TLV_DB_SCALE(sun8i_codec_out_mixer_pregain_scale,
				  -450, 150, 0);
static const DECLARE_TLV_DB_RANGE(sun8i_codec_mic_gain_scale,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 7, TLV_DB_SCALE_ITEM(2400, 300, 0),
);

static const struct snd_kcontrol_new sun8i_codec_common_controls[] = {
	/* Mixer pre-gains */
	SOC_SINGLE_TLV("Line In Playback Volume", SUN8I_ADDA_LINEIN_GCTRL,
		       SUN8I_ADDA_LINEIN_GCTRL_LINEING,
		       0x7, 0, sun8i_codec_out_mixer_pregain_scale),
	SOC_SINGLE_TLV("Mic1 Playback Volume", SUN8I_ADDA_MICIN_GCTRL,
		       SUN8I_ADDA_MICIN_GCTRL_MIC1G,
		       0x7, 0, sun8i_codec_out_mixer_pregain_scale),
	SOC_SINGLE_TLV("Mic2 Playback Volume",
		       SUN8I_ADDA_MICIN_GCTRL, SUN8I_ADDA_MICIN_GCTRL_MIC2G,
		       0x7, 0, sun8i_codec_out_mixer_pregain_scale),

	/* Microphone Amp boost gains */
	SOC_SINGLE_TLV("Mic1 Boost Volume", SUN8I_ADDA_MIC1G_MICBIAS_CTRL,
		       SUN8I_ADDA_MIC1G_MICBIAS_CTRL_MIC1BOOST, 0x7, 0,
		       sun8i_codec_mic_gain_scale),
	SOC_SINGLE_TLV("Mic2 Boost Volume", SUN8I_ADDA_MIC2G_CTRL,
		       SUN8I_ADDA_MIC2G_CTRL_MIC2BOOST, 0x7, 0,
		       sun8i_codec_mic_gain_scale),

	/* ADC */
	SOC_SINGLE_TLV("ADC Gain Capture Volume", SUN8I_ADDA_ADC_AP_EN,
		       SUN8I_ADDA_ADC_AP_EN_ADCG, 0x7, 0,
		       sun8i_codec_out_mixer_pregain_scale),
};

static const struct snd_soc_dapm_widget sun8i_codec_common_widgets[] = {
	/* ADC */
	SND_SOC_DAPM_ADC("Left ADC", NULL, SUN8I_ADDA_ADC_AP_EN,
			 SUN8I_ADDA_ADC_AP_EN_ADCLEN, 0),
	SND_SOC_DAPM_ADC("Right ADC", NULL, SUN8I_ADDA_ADC_AP_EN,
			 SUN8I_ADDA_ADC_AP_EN_ADCREN, 0),

	/* DAC */
	SND_SOC_DAPM_DAC("Left DAC", NULL, SUN8I_ADDA_DAC_PA_SRC,
			 SUN8I_ADDA_DAC_PA_SRC_DACALEN, 0),
	SND_SOC_DAPM_DAC("Right DAC", NULL, SUN8I_ADDA_DAC_PA_SRC,
			 SUN8I_ADDA_DAC_PA_SRC_DACAREN, 0),
	/*
	 * Due to this component and the codec belonging to separate DAPM
	 * contexts, we need to manually link the above widgets to their
	 * stream widgets at the card level.
	 */

	/* Line In */
	SND_SOC_DAPM_INPUT("LINEIN"),

	/* Microphone inputs */
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),

	/* Microphone Bias */
	SND_SOC_DAPM_SUPPLY("MBIAS", SUN8I_ADDA_MIC1G_MICBIAS_CTRL,
			    SUN8I_ADDA_MIC1G_MICBIAS_CTRL_MMICBIASEN,
			    0, NULL, 0),

	/* Mic input path */
	SND_SOC_DAPM_PGA("Mic1 Amplifier", SUN8I_ADDA_MIC1G_MICBIAS_CTRL,
			 SUN8I_ADDA_MIC1G_MICBIAS_CTRL_MIC1AMPEN, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mic2 Amplifier", SUN8I_ADDA_MIC2G_CTRL,
			 SUN8I_ADDA_MIC2G_CTRL_MIC2AMPEN, 0, NULL, 0),

	/* Mixers */
	SND_SOC_DAPM_MIXER("Left Mixer", SUN8I_ADDA_DAC_PA_SRC,
			   SUN8I_ADDA_DAC_PA_SRC_LMIXEN, 0,
			   sun8i_codec_mixer_controls,
			   ARRAY_SIZE(sun8i_codec_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SUN8I_ADDA_DAC_PA_SRC,
			   SUN8I_ADDA_DAC_PA_SRC_RMIXEN, 0,
			   sun8i_codec_mixer_controls,
			   ARRAY_SIZE(sun8i_codec_mixer_controls)),
	SND_SOC_DAPM_MIXER("Left ADC Mixer", SUN8I_ADDA_ADC_AP_EN,
			   SUN8I_ADDA_ADC_AP_EN_ADCLEN, 0,
			   sun8i_codec_adc_mixer_controls,
			   ARRAY_SIZE(sun8i_codec_adc_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right ADC Mixer", SUN8I_ADDA_ADC_AP_EN,
			   SUN8I_ADDA_ADC_AP_EN_ADCREN, 0,
			   sun8i_codec_adc_mixer_controls,
			   ARRAY_SIZE(sun8i_codec_adc_mixer_controls)),
};

static const struct snd_soc_dapm_route sun8i_codec_common_routes[] = {
	/* Microphone Routes */
	{ "Mic1 Amplifier", NULL, "MIC1"},
	{ "Mic2 Amplifier", NULL, "MIC2"},

	/* Left Mixer Routes */
	{ "Left Mixer", "DAC Playback Switch", "Left DAC" },
	{ "Left Mixer", "DAC Reversed Playback Switch", "Right DAC" },
	{ "Left Mixer", "Line In Playback Switch", "LINEIN" },
	{ "Left Mixer", "Mic1 Playback Switch", "Mic1 Amplifier" },
	{ "Left Mixer", "Mic2 Playback Switch", "Mic2 Amplifier" },

	/* Right Mixer Routes */
	{ "Right Mixer", "DAC Playback Switch", "Right DAC" },
	{ "Right Mixer", "DAC Reversed Playback Switch", "Left DAC" },
	{ "Right Mixer", "Line In Playback Switch", "LINEIN" },
	{ "Right Mixer", "Mic1 Playback Switch", "Mic1 Amplifier" },
	{ "Right Mixer", "Mic2 Playback Switch", "Mic2 Amplifier" },

	/* Left ADC Mixer Routes */
	{ "Left ADC Mixer", "Mixer Capture Switch", "Left Mixer" },
	{ "Left ADC Mixer", "Mixer Reversed Capture Switch", "Right Mixer" },
	{ "Left ADC Mixer", "Line In Capture Switch", "LINEIN" },
	{ "Left ADC Mixer", "Mic1 Capture Switch", "Mic1 Amplifier" },
	{ "Left ADC Mixer", "Mic2 Capture Switch", "Mic2 Amplifier" },

	/* Right ADC Mixer Routes */
	{ "Right ADC Mixer", "Mixer Capture Switch", "Right Mixer" },
	{ "Right ADC Mixer", "Mixer Reversed Capture Switch", "Left Mixer" },
	{ "Right ADC Mixer", "Line In Capture Switch", "LINEIN" },
	{ "Right ADC Mixer", "Mic1 Capture Switch", "Mic1 Amplifier" },
	{ "Right ADC Mixer", "Mic2 Capture Switch", "Mic2 Amplifier" },

	/* ADC Routes */
	{ "Left ADC", NULL, "Left ADC Mixer" },
	{ "Right ADC", NULL, "Right ADC Mixer" },
};

/* headphone specific controls, widgets, and routes */
static const DECLARE_TLV_DB_SCALE(sun8i_codec_hp_vol_scale, -6300, 100, 1);
static const struct snd_kcontrol_new sun8i_codec_headphone_controls[] = {
	SOC_SINGLE_TLV("Headphone Playback Volume",
		       SUN8I_ADDA_HP_VOLC,
		       SUN8I_ADDA_HP_VOLC_HP_VOL, 0x3f, 0,
		       sun8i_codec_hp_vol_scale),
	SOC_DOUBLE("Headphone Playback Switch",
		   SUN8I_ADDA_DAC_PA_SRC,
		   SUN8I_ADDA_DAC_PA_SRC_LHPPAMUTE,
		   SUN8I_ADDA_DAC_PA_SRC_RHPPAMUTE, 1, 0),
};

static const char * const sun8i_codec_hp_src_enum_text[] = {
	"DAC", "Mixer",
};

static SOC_ENUM_DOUBLE_DECL(sun8i_codec_hp_src_enum,
			    SUN8I_ADDA_DAC_PA_SRC,
			    SUN8I_ADDA_DAC_PA_SRC_LHPIS,
			    SUN8I_ADDA_DAC_PA_SRC_RHPIS,
			    sun8i_codec_hp_src_enum_text);

static const struct snd_kcontrol_new sun8i_codec_hp_src[] = {
	SOC_DAPM_ENUM("Headphone Source Playback Route",
		      sun8i_codec_hp_src_enum),
};

static int sun8i_headphone_amp_event(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *k, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		snd_soc_component_update_bits(component, SUN8I_ADDA_PAEN_HP_CTRL,
					      BIT(SUN8I_ADDA_PAEN_HP_CTRL_HPPAEN),
					      BIT(SUN8I_ADDA_PAEN_HP_CTRL_HPPAEN));
		/*
		 * Need a delay to have the amplifier up. 700ms seems the best
		 * compromise between the time to let the amplifier up and the
		 * time not to feel this delay while playing a sound.
		 */
		msleep(700);
	} else if (SND_SOC_DAPM_EVENT_OFF(event)) {
		snd_soc_component_update_bits(component, SUN8I_ADDA_PAEN_HP_CTRL,
					      BIT(SUN8I_ADDA_PAEN_HP_CTRL_HPPAEN),
					      0x0);
	}

	return 0;
}

static const struct snd_soc_dapm_widget sun8i_codec_headphone_widgets[] = {
	SND_SOC_DAPM_MUX("Headphone Source Playback Route",
			 SND_SOC_NOPM, 0, 0, sun8i_codec_hp_src),
	SND_SOC_DAPM_OUT_DRV_E("Headphone Amp", SUN8I_ADDA_PAEN_HP_CTRL,
			       SUN8I_ADDA_PAEN_HP_CTRL_HPPAEN, 0, NULL, 0,
			       sun8i_headphone_amp_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("HPCOM Protection", SUN8I_ADDA_PAEN_HP_CTRL,
			    SUN8I_ADDA_PAEN_HP_CTRL_COMPTEN, 0, NULL, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_supply, "HPCOM", SUN8I_ADDA_PAEN_HP_CTRL,
			 SUN8I_ADDA_PAEN_HP_CTRL_HPCOM_FC, 0x3, 0x3, 0),
	SND_SOC_DAPM_OUTPUT("HP"),
};

static const struct snd_soc_dapm_route sun8i_codec_headphone_routes[] = {
	{ "Headphone Source Playback Route", "DAC", "Left DAC" },
	{ "Headphone Source Playback Route", "DAC", "Right DAC" },
	{ "Headphone Source Playback Route", "Mixer", "Left Mixer" },
	{ "Headphone Source Playback Route", "Mixer", "Right Mixer" },
	{ "Headphone Amp", NULL, "Headphone Source Playback Route" },
	{ "HPCOM", NULL, "HPCOM Protection" },
	{ "HP", NULL, "Headphone Amp" },
};

static int sun8i_codec_add_headphone(struct snd_soc_component *cmpnt)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(cmpnt);
	struct device *dev = cmpnt->dev;
	int ret;

	ret = snd_soc_add_component_controls(cmpnt,
					     sun8i_codec_headphone_controls,
					     ARRAY_SIZE(sun8i_codec_headphone_controls));
	if (ret) {
		dev_err(dev, "Failed to add Headphone controls: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_new_controls(dapm, sun8i_codec_headphone_widgets,
					ARRAY_SIZE(sun8i_codec_headphone_widgets));
	if (ret) {
		dev_err(dev, "Failed to add Headphone DAPM widgets: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(dapm, sun8i_codec_headphone_routes,
				      ARRAY_SIZE(sun8i_codec_headphone_routes));
	if (ret) {
		dev_err(dev, "Failed to add Headphone DAPM routes: %d\n", ret);
		return ret;
	}

	return 0;
}

/* hmic specific widget */
static const struct snd_soc_dapm_widget sun8i_codec_hmic_widgets[] = {
	SND_SOC_DAPM_SUPPLY("HBIAS", SUN8I_ADDA_MIC1G_MICBIAS_CTRL,
			    SUN8I_ADDA_MIC1G_MICBIAS_CTRL_HMICBIASEN,
			    0, NULL, 0),
};

static int sun8i_codec_add_hmic(struct snd_soc_component *cmpnt)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(cmpnt);
	struct device *dev = cmpnt->dev;
	int ret;

	ret = snd_soc_dapm_new_controls(dapm, sun8i_codec_hmic_widgets,
					ARRAY_SIZE(sun8i_codec_hmic_widgets));
	if (ret)
		dev_err(dev, "Failed to add Mic3 DAPM widgets: %d\n", ret);

	return ret;
}

/* line out specific controls, widgets and routes */
static const DECLARE_TLV_DB_RANGE(sun8i_codec_lineout_vol_scale,
	0, 1, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
	2, 31, TLV_DB_SCALE_ITEM(-4350, 150, 0),
);
static const struct snd_kcontrol_new sun8i_codec_lineout_controls[] = {
	SOC_SINGLE_TLV("Line Out Playback Volume",
		       SUN8I_ADDA_PHONE_GAIN_CTRL,
		       SUN8I_ADDA_PHONE_GAIN_CTRL_LINEOUT_VOL, 0x1f, 0,
		       sun8i_codec_lineout_vol_scale),
	SOC_DOUBLE("Line Out Playback Switch",
		   SUN8I_ADDA_MIC2G_CTRL,
		   SUN8I_ADDA_MIC2G_CTRL_LINEOUTLEN,
		   SUN8I_ADDA_MIC2G_CTRL_LINEOUTREN, 1, 0),
};

static const char * const sun8i_codec_lineout_src_enum_text[] = {
	"Stereo", "Mono Differential",
};

static SOC_ENUM_DOUBLE_DECL(sun8i_codec_lineout_src_enum,
			    SUN8I_ADDA_MIC2G_CTRL,
			    SUN8I_ADDA_MIC2G_CTRL_LINEOUTLSRC,
			    SUN8I_ADDA_MIC2G_CTRL_LINEOUTRSRC,
			    sun8i_codec_lineout_src_enum_text);

static const struct snd_kcontrol_new sun8i_codec_lineout_src[] = {
	SOC_DAPM_ENUM("Line Out Source Playback Route",
		      sun8i_codec_lineout_src_enum),
};

static const struct snd_soc_dapm_widget sun8i_codec_lineout_widgets[] = {
	SND_SOC_DAPM_MUX("Line Out Source Playback Route",
			 SND_SOC_NOPM, 0, 0, sun8i_codec_lineout_src),
	/* It is unclear if this is a buffer or gate, model it as a supply */
	SND_SOC_DAPM_SUPPLY("Line Out Enable", SUN8I_ADDA_PAEN_HP_CTRL,
			    SUN8I_ADDA_PAEN_HP_CTRL_LINEOUTEN, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("LINEOUT"),
};

static const struct snd_soc_dapm_route sun8i_codec_lineout_routes[] = {
	{ "Line Out Source Playback Route", "Stereo", "Left Mixer" },
	{ "Line Out Source Playback Route", "Stereo", "Right Mixer" },
	{ "Line Out Source Playback Route", "Mono Differential", "Left Mixer" },
	{ "Line Out Source Playback Route", "Mono Differential", "Right Mixer" },
	{ "LINEOUT", NULL, "Line Out Source Playback Route" },
	{ "LINEOUT", NULL, "Line Out Enable", },
};

static int sun8i_codec_add_lineout(struct snd_soc_component *cmpnt)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(cmpnt);
	struct device *dev = cmpnt->dev;
	int ret;

	ret = snd_soc_add_component_controls(cmpnt,
					     sun8i_codec_lineout_controls,
					     ARRAY_SIZE(sun8i_codec_lineout_controls));
	if (ret) {
		dev_err(dev, "Failed to add Line Out controls: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_new_controls(dapm, sun8i_codec_lineout_widgets,
					ARRAY_SIZE(sun8i_codec_lineout_widgets));
	if (ret) {
		dev_err(dev, "Failed to add Line Out DAPM widgets: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(dapm, sun8i_codec_lineout_routes,
				      ARRAY_SIZE(sun8i_codec_lineout_routes));
	if (ret) {
		dev_err(dev, "Failed to add Line Out DAPM routes: %d\n", ret);
		return ret;
	}

	return 0;
}

struct sun8i_codec_analog_quirks {
	bool has_headphone;
	bool has_hmic;
	bool has_lineout;
};

static const struct sun8i_codec_analog_quirks sun8i_a23_quirks = {
	.has_headphone	= true,
	.has_hmic	= true,
};

static const struct sun8i_codec_analog_quirks sun8i_h3_quirks = {
	.has_lineout	= true,
};

static int sun8i_codec_analog_cmpnt_probe(struct snd_soc_component *cmpnt)
{
	struct device *dev = cmpnt->dev;
	const struct sun8i_codec_analog_quirks *quirks;
	int ret;

	/*
	 * This would never return NULL unless someone directly registers a
	 * platform device matching this driver's name, without specifying a
	 * device tree node.
	 */
	quirks = of_device_get_match_data(dev);

	/* Add controls, widgets, and routes for individual features */

	if (quirks->has_headphone) {
		ret = sun8i_codec_add_headphone(cmpnt);
		if (ret)
			return ret;
	}

	if (quirks->has_hmic) {
		ret = sun8i_codec_add_hmic(cmpnt);
		if (ret)
			return ret;
	}

	if (quirks->has_lineout) {
		ret = sun8i_codec_add_lineout(cmpnt);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct snd_soc_component_driver sun8i_codec_analog_cmpnt_drv = {
	.controls		= sun8i_codec_common_controls,
	.num_controls		= ARRAY_SIZE(sun8i_codec_common_controls),
	.dapm_widgets		= sun8i_codec_common_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sun8i_codec_common_widgets),
	.dapm_routes		= sun8i_codec_common_routes,
	.num_dapm_routes	= ARRAY_SIZE(sun8i_codec_common_routes),
	.probe			= sun8i_codec_analog_cmpnt_probe,
};

static const struct of_device_id sun8i_codec_analog_of_match[] = {
	{
		.compatible = "allwinner,sun8i-a23-codec-analog",
		.data = &sun8i_a23_quirks,
	},
	{
		.compatible = "allwinner,sun8i-h3-codec-analog",
		.data = &sun8i_h3_quirks,
	},
	{}
};
MODULE_DEVICE_TABLE(of, sun8i_codec_analog_of_match);

static int sun8i_codec_analog_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct regmap *regmap;
	void __iomem *base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "Failed to map the registers\n");
		return PTR_ERR(base);
	}

	regmap = devm_regmap_init(&pdev->dev, NULL, base, &adda_pr_regmap_cfg);
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "Failed to create regmap\n");
		return PTR_ERR(regmap);
	}

	return devm_snd_soc_register_component(&pdev->dev,
					       &sun8i_codec_analog_cmpnt_drv,
					       NULL, 0);
}

static struct platform_driver sun8i_codec_analog_driver = {
	.driver = {
		.name = "sun8i-codec-analog",
		.of_match_table = sun8i_codec_analog_of_match,
	},
	.probe = sun8i_codec_analog_probe,
};
module_platform_driver(sun8i_codec_analog_driver);

MODULE_DESCRIPTION("Allwinner internal codec analog controls driver");
MODULE_AUTHOR("Chen-Yu Tsai <wens@csie.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sun8i-codec-analog");
