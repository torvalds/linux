// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs42l73.c  --  CS42L73 ALSA Soc Audio driver
 *
 * Copyright 2011 Cirrus Logic, Inc.
 *
 * Authors: Georgi Vlaev, Nucleus Systems Ltd, <joe@nucleusys.com>
 *	    Brian Austin, Cirrus Logic Inc, <brian.austin@cirrus.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/cs42l73.h>
#include "cs42l73.h"
#include "cirrus_legacy.h"

struct sp_config {
	u8 spc, mmcc, spfs;
	u32 srate;
};
struct  cs42l73_private {
	struct cs42l73_platform_data pdata;
	struct sp_config config[3];
	struct regmap *regmap;
	u32 sysclk;
	u8 mclksel;
	u32 mclk;
	int shutdwn_delay;
};

static const struct reg_default cs42l73_reg_defaults[] = {
	{ 6, 0xF1 },	/* r06	- Power Ctl 1 */
	{ 7, 0xDF },	/* r07	- Power Ctl 2 */
	{ 8, 0x3F },	/* r08	- Power Ctl 3 */
	{ 9, 0x50 },	/* r09	- Charge Pump Freq */
	{ 10, 0x53 },	/* r0A	- Output Load MicBias Short Detect */
	{ 11, 0x00 },	/* r0B	- DMIC Master Clock Ctl */
	{ 12, 0x00 },	/* r0C	- Aux PCM Ctl */
	{ 13, 0x15 },	/* r0D	- Aux PCM Master Clock Ctl */
	{ 14, 0x00 },	/* r0E	- Audio PCM Ctl */
	{ 15, 0x15 },	/* r0F	- Audio PCM Master Clock Ctl */
	{ 16, 0x00 },	/* r10	- Voice PCM Ctl */
	{ 17, 0x15 },	/* r11	- Voice PCM Master Clock Ctl */
	{ 18, 0x00 },	/* r12	- Voice/Aux Sample Rate */
	{ 19, 0x06 },	/* r13	- Misc I/O Path Ctl */
	{ 20, 0x00 },	/* r14	- ADC Input Path Ctl */
	{ 21, 0x00 },	/* r15	- MICA Preamp, PGA Volume */
	{ 22, 0x00 },	/* r16	- MICB Preamp, PGA Volume */
	{ 23, 0x00 },	/* r17	- Input Path A Digital Volume */
	{ 24, 0x00 },	/* r18	- Input Path B Digital Volume */
	{ 25, 0x00 },	/* r19	- Playback Digital Ctl */
	{ 26, 0x00 },	/* r1A	- HP/LO Left Digital Volume */
	{ 27, 0x00 },	/* r1B	- HP/LO Right Digital Volume */
	{ 28, 0x00 },	/* r1C	- Speakerphone Digital Volume */
	{ 29, 0x00 },	/* r1D	- Ear/SPKLO Digital Volume */
	{ 30, 0x00 },	/* r1E	- HP Left Analog Volume */
	{ 31, 0x00 },	/* r1F	- HP Right Analog Volume */
	{ 32, 0x00 },	/* r20	- LO Left Analog Volume */
	{ 33, 0x00 },	/* r21	- LO Right Analog Volume */
	{ 34, 0x00 },	/* r22	- Stereo Input Path Advisory Volume */
	{ 35, 0x00 },	/* r23	- Aux PCM Input Advisory Volume */
	{ 36, 0x00 },	/* r24	- Audio PCM Input Advisory Volume */
	{ 37, 0x00 },	/* r25	- Voice PCM Input Advisory Volume */
	{ 38, 0x00 },	/* r26	- Limiter Attack Rate HP/LO */
	{ 39, 0x7F },	/* r27	- Limter Ctl, Release Rate HP/LO */
	{ 40, 0x00 },	/* r28	- Limter Threshold HP/LO */
	{ 41, 0x00 },	/* r29	- Limiter Attack Rate Speakerphone */
	{ 42, 0x3F },	/* r2A	- Limter Ctl, Release Rate Speakerphone */
	{ 43, 0x00 },	/* r2B	- Limter Threshold Speakerphone */
	{ 44, 0x00 },	/* r2C	- Limiter Attack Rate Ear/SPKLO */
	{ 45, 0x3F },	/* r2D	- Limter Ctl, Release Rate Ear/SPKLO */
	{ 46, 0x00 },	/* r2E	- Limter Threshold Ear/SPKLO */
	{ 47, 0x00 },	/* r2F	- ALC Enable, Attack Rate Left/Right */
	{ 48, 0x3F },	/* r30	- ALC Release Rate Left/Right */
	{ 49, 0x00 },	/* r31	- ALC Threshold Left/Right */
	{ 50, 0x00 },	/* r32	- Noise Gate Ctl Left/Right */
	{ 51, 0x00 },	/* r33	- ALC/NG Misc Ctl */
	{ 52, 0x18 },	/* r34	- Mixer Ctl */
	{ 53, 0x3F },	/* r35	- HP/LO Left Mixer Input Path Volume */
	{ 54, 0x3F },	/* r36	- HP/LO Right Mixer Input Path Volume */
	{ 55, 0x3F },	/* r37	- HP/LO Left Mixer Aux PCM Volume */
	{ 56, 0x3F },	/* r38	- HP/LO Right Mixer Aux PCM Volume */
	{ 57, 0x3F },	/* r39	- HP/LO Left Mixer Audio PCM Volume */
	{ 58, 0x3F },	/* r3A	- HP/LO Right Mixer Audio PCM Volume */
	{ 59, 0x3F },	/* r3B	- HP/LO Left Mixer Voice PCM Mono Volume */
	{ 60, 0x3F },	/* r3C	- HP/LO Right Mixer Voice PCM Mono Volume */
	{ 61, 0x3F },	/* r3D	- Aux PCM Left Mixer Input Path Volume */
	{ 62, 0x3F },	/* r3E	- Aux PCM Right Mixer Input Path Volume */
	{ 63, 0x3F },	/* r3F	- Aux PCM Left Mixer Volume */
	{ 64, 0x3F },	/* r40	- Aux PCM Left Mixer Volume */
	{ 65, 0x3F },	/* r41	- Aux PCM Left Mixer Audio PCM L Volume */
	{ 66, 0x3F },	/* r42	- Aux PCM Right Mixer Audio PCM R Volume */
	{ 67, 0x3F },	/* r43	- Aux PCM Left Mixer Voice PCM Volume */
	{ 68, 0x3F },	/* r44	- Aux PCM Right Mixer Voice PCM Volume */
	{ 69, 0x3F },	/* r45	- Audio PCM Left Input Path Volume */
	{ 70, 0x3F },	/* r46	- Audio PCM Right Input Path Volume */
	{ 71, 0x3F },	/* r47	- Audio PCM Left Mixer Aux PCM L Volume */
	{ 72, 0x3F },	/* r48	- Audio PCM Right Mixer Aux PCM R Volume */
	{ 73, 0x3F },	/* r49	- Audio PCM Left Mixer Volume */
	{ 74, 0x3F },	/* r4A	- Audio PCM Right Mixer Volume */
	{ 75, 0x3F },	/* r4B	- Audio PCM Left Mixer Voice PCM Volume */
	{ 76, 0x3F },	/* r4C	- Audio PCM Right Mixer Voice PCM Volume */
	{ 77, 0x3F },	/* r4D	- Voice PCM Left Input Path Volume */
	{ 78, 0x3F },	/* r4E	- Voice PCM Right Input Path Volume */
	{ 79, 0x3F },	/* r4F	- Voice PCM Left Mixer Aux PCM L Volume */
	{ 80, 0x3F },	/* r50	- Voice PCM Right Mixer Aux PCM R Volume */
	{ 81, 0x3F },	/* r51	- Voice PCM Left Mixer Audio PCM L Volume */
	{ 82, 0x3F },	/* r52	- Voice PCM Right Mixer Audio PCM R Volume */
	{ 83, 0x3F },	/* r53	- Voice PCM Left Mixer Voice PCM Volume */
	{ 84, 0x3F },	/* r54	- Voice PCM Right Mixer Voice PCM Volume */
	{ 85, 0xAA },	/* r55	- Mono Mixer Ctl */
	{ 86, 0x3F },	/* r56	- SPK Mono Mixer Input Path Volume */
	{ 87, 0x3F },	/* r57	- SPK Mono Mixer Aux PCM Mono/L/R Volume */
	{ 88, 0x3F },	/* r58	- SPK Mono Mixer Audio PCM Mono/L/R Volume */
	{ 89, 0x3F },	/* r59	- SPK Mono Mixer Voice PCM Mono Volume */
	{ 90, 0x3F },	/* r5A	- SPKLO Mono Mixer Input Path Mono Volume */
	{ 91, 0x3F },	/* r5B	- SPKLO Mono Mixer Aux Mono/L/R Volume */
	{ 92, 0x3F },	/* r5C	- SPKLO Mono Mixer Audio Mono/L/R Volume */
	{ 93, 0x3F },	/* r5D	- SPKLO Mono Mixer Voice Mono Volume */
	{ 94, 0x00 },	/* r5E	- Interrupt Mask 1 */
	{ 95, 0x00 },	/* r5F	- Interrupt Mask 2 */
};

static bool cs42l73_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L73_IS1:
	case CS42L73_IS2:
		return true;
	default:
		return false;
	}
}

static bool cs42l73_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L73_DEVID_AB ... CS42L73_DEVID_E:
	case CS42L73_REVID ... CS42L73_IM2:
		return true;
	default:
		return false;
	}
}

static const DECLARE_TLV_DB_RANGE(hpaloa_tlv,
	0, 13, TLV_DB_SCALE_ITEM(-7600, 200, 0),
	14, 75, TLV_DB_SCALE_ITEM(-4900, 100, 0)
);

static DECLARE_TLV_DB_SCALE(adc_boost_tlv, 0, 2500, 0);

static DECLARE_TLV_DB_SCALE(hl_tlv, -10200, 50, 0);

static DECLARE_TLV_DB_SCALE(ipd_tlv, -9600, 100, 0);

static DECLARE_TLV_DB_SCALE(micpga_tlv, -600, 50, 0);

static const DECLARE_TLV_DB_RANGE(limiter_tlv,
	0, 2, TLV_DB_SCALE_ITEM(-3000, 600, 0),
	3, 7, TLV_DB_SCALE_ITEM(-1200, 300, 0)
);

static const DECLARE_TLV_DB_SCALE(attn_tlv, -6300, 100, 1);

static const char * const cs42l73_pgaa_text[] = { "Line A", "Mic 1" };
static const char * const cs42l73_pgab_text[] = { "Line B", "Mic 2" };

static SOC_ENUM_SINGLE_DECL(pgaa_enum,
			    CS42L73_ADCIPC, 3,
			    cs42l73_pgaa_text);

static SOC_ENUM_SINGLE_DECL(pgab_enum,
			    CS42L73_ADCIPC, 7,
			    cs42l73_pgab_text);

static const struct snd_kcontrol_new pgaa_mux =
	SOC_DAPM_ENUM("Left Analog Input Capture Mux", pgaa_enum);

static const struct snd_kcontrol_new pgab_mux =
	SOC_DAPM_ENUM("Right Analog Input Capture Mux", pgab_enum);

static const struct snd_kcontrol_new input_left_mixer[] = {
	SOC_DAPM_SINGLE("ADC Left Input", CS42L73_PWRCTL1,
			5, 1, 1),
	SOC_DAPM_SINGLE("DMIC Left Input", CS42L73_PWRCTL1,
			4, 1, 1),
};

static const struct snd_kcontrol_new input_right_mixer[] = {
	SOC_DAPM_SINGLE("ADC Right Input", CS42L73_PWRCTL1,
			7, 1, 1),
	SOC_DAPM_SINGLE("DMIC Right Input", CS42L73_PWRCTL1,
			6, 1, 1),
};

static const char * const cs42l73_ng_delay_text[] = {
	"50ms", "100ms", "150ms", "200ms" };

static SOC_ENUM_SINGLE_DECL(ng_delay_enum,
			    CS42L73_NGCAB, 0,
			    cs42l73_ng_delay_text);

static const char * const cs42l73_mono_mix_texts[] = {
	"Left", "Right", "Mono Mix"};

static const unsigned int cs42l73_mono_mix_values[] = { 0, 1, 2 };

static const struct soc_enum spk_asp_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L73_MMIXCTL, 6, 3,
			      ARRAY_SIZE(cs42l73_mono_mix_texts),
			      cs42l73_mono_mix_texts,
			      cs42l73_mono_mix_values);

static const struct snd_kcontrol_new spk_asp_mixer =
	SOC_DAPM_ENUM("Route", spk_asp_enum);

static const struct soc_enum spk_xsp_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L73_MMIXCTL, 4, 3,
			      ARRAY_SIZE(cs42l73_mono_mix_texts),
			      cs42l73_mono_mix_texts,
			      cs42l73_mono_mix_values);

static const struct snd_kcontrol_new spk_xsp_mixer =
	SOC_DAPM_ENUM("Route", spk_xsp_enum);

static const struct soc_enum esl_asp_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L73_MMIXCTL, 2, 3,
			      ARRAY_SIZE(cs42l73_mono_mix_texts),
			      cs42l73_mono_mix_texts,
			      cs42l73_mono_mix_values);

static const struct snd_kcontrol_new esl_asp_mixer =
	SOC_DAPM_ENUM("Route", esl_asp_enum);

static const struct soc_enum esl_xsp_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L73_MMIXCTL, 0, 3,
			      ARRAY_SIZE(cs42l73_mono_mix_texts),
			      cs42l73_mono_mix_texts,
			      cs42l73_mono_mix_values);

static const struct snd_kcontrol_new esl_xsp_mixer =
	SOC_DAPM_ENUM("Route", esl_xsp_enum);

static const char * const cs42l73_ip_swap_text[] = {
	"Stereo", "Mono A", "Mono B", "Swap A-B"};

static SOC_ENUM_SINGLE_DECL(ip_swap_enum,
			    CS42L73_MIOPC, 6,
			    cs42l73_ip_swap_text);

static const char * const cs42l73_spo_mixer_text[] = {"Mono", "Stereo"};

static SOC_ENUM_SINGLE_DECL(vsp_output_mux_enum,
			    CS42L73_MIXERCTL, 5,
			    cs42l73_spo_mixer_text);

static SOC_ENUM_SINGLE_DECL(xsp_output_mux_enum,
			    CS42L73_MIXERCTL, 4,
			    cs42l73_spo_mixer_text);

static const struct snd_kcontrol_new hp_amp_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L73_PWRCTL3, 0, 1, 1);

static const struct snd_kcontrol_new lo_amp_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L73_PWRCTL3, 1, 1, 1);

static const struct snd_kcontrol_new spk_amp_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L73_PWRCTL3, 2, 1, 1);

static const struct snd_kcontrol_new spklo_amp_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L73_PWRCTL3, 4, 1, 1);

static const struct snd_kcontrol_new ear_amp_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L73_PWRCTL3, 3, 1, 1);

static const struct snd_kcontrol_new cs42l73_snd_controls[] = {
	SOC_DOUBLE_R_SX_TLV("Headphone Analog Playback Volume",
			CS42L73_HPAAVOL, CS42L73_HPBAVOL, 0,
			0x41, 0x4B, hpaloa_tlv),

	SOC_DOUBLE_R_SX_TLV("LineOut Analog Playback Volume", CS42L73_LOAAVOL,
			CS42L73_LOBAVOL, 0, 0x41, 0x4B, hpaloa_tlv),

	SOC_DOUBLE_R_SX_TLV("Input PGA Analog Volume", CS42L73_MICAPREPGAAVOL,
			CS42L73_MICBPREPGABVOL, 0, 0x34,
			0x24, micpga_tlv),

	SOC_DOUBLE_R("MIC Preamp Switch", CS42L73_MICAPREPGAAVOL,
			CS42L73_MICBPREPGABVOL, 6, 1, 1),

	SOC_DOUBLE_R_SX_TLV("Input Path Digital Volume", CS42L73_IPADVOL,
			CS42L73_IPBDVOL, 0, 0xA0, 0x6C, ipd_tlv),

	SOC_DOUBLE_R_SX_TLV("HL Digital Playback Volume",
			CS42L73_HLADVOL, CS42L73_HLBDVOL,
			0, 0x34, 0xE4, hl_tlv),

	SOC_SINGLE_TLV("ADC A Boost Volume",
			CS42L73_ADCIPC, 2, 0x01, 1, adc_boost_tlv),

	SOC_SINGLE_TLV("ADC B Boost Volume",
		       CS42L73_ADCIPC, 6, 0x01, 1, adc_boost_tlv),

	SOC_SINGLE_SX_TLV("Speakerphone Digital Volume",
			    CS42L73_SPKDVOL, 0, 0x34, 0xE4, hl_tlv),

	SOC_SINGLE_SX_TLV("Ear Speaker Digital Volume",
			    CS42L73_ESLDVOL, 0, 0x34, 0xE4, hl_tlv),

	SOC_DOUBLE_R("Headphone Analog Playback Switch", CS42L73_HPAAVOL,
			CS42L73_HPBAVOL, 7, 1, 1),

	SOC_DOUBLE_R("LineOut Analog Playback Switch", CS42L73_LOAAVOL,
			CS42L73_LOBAVOL, 7, 1, 1),
	SOC_DOUBLE("Input Path Digital Switch", CS42L73_ADCIPC, 0, 4, 1, 1),
	SOC_DOUBLE("HL Digital Playback Switch", CS42L73_PBDC, 0,
			1, 1, 1),
	SOC_SINGLE("Speakerphone Digital Playback Switch", CS42L73_PBDC, 2, 1,
			1),
	SOC_SINGLE("Ear Speaker Digital Playback Switch", CS42L73_PBDC, 3, 1,
			1),

	SOC_SINGLE("PGA Soft-Ramp Switch", CS42L73_MIOPC, 3, 1, 0),
	SOC_SINGLE("Analog Zero Cross Switch", CS42L73_MIOPC, 2, 1, 0),
	SOC_SINGLE("Digital Soft-Ramp Switch", CS42L73_MIOPC, 1, 1, 0),
	SOC_SINGLE("Analog Output Soft-Ramp Switch", CS42L73_MIOPC, 0, 1, 0),

	SOC_DOUBLE("ADC Signal Polarity Switch", CS42L73_ADCIPC, 1, 5, 1,
			0),

	SOC_SINGLE("HL Limiter Attack Rate", CS42L73_LIMARATEHL, 0, 0x3F,
			0),
	SOC_SINGLE("HL Limiter Release Rate", CS42L73_LIMRRATEHL, 0,
			0x3F, 0),


	SOC_SINGLE("HL Limiter Switch", CS42L73_LIMRRATEHL, 7, 1, 0),
	SOC_SINGLE("HL Limiter All Channels Switch", CS42L73_LIMRRATEHL, 6, 1,
			0),

	SOC_SINGLE_TLV("HL Limiter Max Threshold Volume", CS42L73_LMAXHL, 5, 7,
			1, limiter_tlv),

	SOC_SINGLE_TLV("HL Limiter Cushion Volume", CS42L73_LMAXHL, 2, 7, 1,
			limiter_tlv),

	SOC_SINGLE("SPK Limiter Attack Rate Volume", CS42L73_LIMARATESPK, 0,
			0x3F, 0),
	SOC_SINGLE("SPK Limiter Release Rate Volume", CS42L73_LIMRRATESPK, 0,
			0x3F, 0),
	SOC_SINGLE("SPK Limiter Switch", CS42L73_LIMRRATESPK, 7, 1, 0),
	SOC_SINGLE("SPK Limiter All Channels Switch", CS42L73_LIMRRATESPK,
			6, 1, 0),
	SOC_SINGLE_TLV("SPK Limiter Max Threshold Volume", CS42L73_LMAXSPK, 5,
			7, 1, limiter_tlv),

	SOC_SINGLE_TLV("SPK Limiter Cushion Volume", CS42L73_LMAXSPK, 2, 7, 1,
			limiter_tlv),

	SOC_SINGLE("ESL Limiter Attack Rate Volume", CS42L73_LIMARATEESL, 0,
			0x3F, 0),
	SOC_SINGLE("ESL Limiter Release Rate Volume", CS42L73_LIMRRATEESL, 0,
			0x3F, 0),
	SOC_SINGLE("ESL Limiter Switch", CS42L73_LIMRRATEESL, 7, 1, 0),
	SOC_SINGLE_TLV("ESL Limiter Max Threshold Volume", CS42L73_LMAXESL, 5,
			7, 1, limiter_tlv),

	SOC_SINGLE_TLV("ESL Limiter Cushion Volume", CS42L73_LMAXESL, 2, 7, 1,
			limiter_tlv),

	SOC_SINGLE("ALC Attack Rate Volume", CS42L73_ALCARATE, 0, 0x3F, 0),
	SOC_SINGLE("ALC Release Rate Volume", CS42L73_ALCRRATE, 0, 0x3F, 0),
	SOC_DOUBLE("ALC Switch", CS42L73_ALCARATE, 6, 7, 1, 0),
	SOC_SINGLE_TLV("ALC Max Threshold Volume", CS42L73_ALCMINMAX, 5, 7, 0,
			limiter_tlv),
	SOC_SINGLE_TLV("ALC Min Threshold Volume", CS42L73_ALCMINMAX, 2, 7, 0,
			limiter_tlv),

	SOC_DOUBLE("NG Enable Switch", CS42L73_NGCAB, 6, 7, 1, 0),
	SOC_SINGLE("NG Boost Switch", CS42L73_NGCAB, 5, 1, 0),
	/*
	    NG Threshold depends on NG_BOOTSAB, which selects
	    between two threshold scales in decibels.
	    Set linear values for now ..
	*/
	SOC_SINGLE("NG Threshold", CS42L73_NGCAB, 2, 7, 0),
	SOC_ENUM("NG Delay", ng_delay_enum),

	SOC_DOUBLE_R_TLV("XSP-IP Volume",
			CS42L73_XSPAIPAA, CS42L73_XSPBIPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("XSP-XSP Volume",
			CS42L73_XSPAXSPAA, CS42L73_XSPBXSPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("XSP-ASP Volume",
			CS42L73_XSPAASPAA, CS42L73_XSPAASPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("XSP-VSP Volume",
			CS42L73_XSPAVSPMA, CS42L73_XSPBVSPMA, 0, 0x3F, 1,
			attn_tlv),

	SOC_DOUBLE_R_TLV("ASP-IP Volume",
			CS42L73_ASPAIPAA, CS42L73_ASPBIPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("ASP-XSP Volume",
			CS42L73_ASPAXSPAA, CS42L73_ASPBXSPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("ASP-ASP Volume",
			CS42L73_ASPAASPAA, CS42L73_ASPBASPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("ASP-VSP Volume",
			CS42L73_ASPAVSPMA, CS42L73_ASPBVSPMA, 0, 0x3F, 1,
			attn_tlv),

	SOC_DOUBLE_R_TLV("VSP-IP Volume",
			CS42L73_VSPAIPAA, CS42L73_VSPBIPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("VSP-XSP Volume",
			CS42L73_VSPAXSPAA, CS42L73_VSPBXSPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("VSP-ASP Volume",
			CS42L73_VSPAASPAA, CS42L73_VSPBASPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("VSP-VSP Volume",
			CS42L73_VSPAVSPMA, CS42L73_VSPBVSPMA, 0, 0x3F, 1,
			attn_tlv),

	SOC_DOUBLE_R_TLV("HL-IP Volume",
			CS42L73_HLAIPAA, CS42L73_HLBIPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("HL-XSP Volume",
			CS42L73_HLAXSPAA, CS42L73_HLBXSPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("HL-ASP Volume",
			CS42L73_HLAASPAA, CS42L73_HLBASPBA, 0, 0x3F, 1,
			attn_tlv),
	SOC_DOUBLE_R_TLV("HL-VSP Volume",
			CS42L73_HLAVSPMA, CS42L73_HLBVSPMA, 0, 0x3F, 1,
			attn_tlv),

	SOC_SINGLE_TLV("SPK-IP Mono Volume",
			CS42L73_SPKMIPMA, 0, 0x3F, 1, attn_tlv),
	SOC_SINGLE_TLV("SPK-XSP Mono Volume",
			CS42L73_SPKMXSPA, 0, 0x3F, 1, attn_tlv),
	SOC_SINGLE_TLV("SPK-ASP Mono Volume",
			CS42L73_SPKMASPA, 0, 0x3F, 1, attn_tlv),
	SOC_SINGLE_TLV("SPK-VSP Mono Volume",
			CS42L73_SPKMVSPMA, 0, 0x3F, 1, attn_tlv),

	SOC_SINGLE_TLV("ESL-IP Mono Volume",
			CS42L73_ESLMIPMA, 0, 0x3F, 1, attn_tlv),
	SOC_SINGLE_TLV("ESL-XSP Mono Volume",
			CS42L73_ESLMXSPA, 0, 0x3F, 1, attn_tlv),
	SOC_SINGLE_TLV("ESL-ASP Mono Volume",
			CS42L73_ESLMASPA, 0, 0x3F, 1, attn_tlv),
	SOC_SINGLE_TLV("ESL-VSP Mono Volume",
			CS42L73_ESLMVSPMA, 0, 0x3F, 1, attn_tlv),

	SOC_ENUM("IP Digital Swap/Mono Select", ip_swap_enum),

	SOC_ENUM("VSPOUT Mono/Stereo Select", vsp_output_mux_enum),
	SOC_ENUM("XSPOUT Mono/Stereo Select", xsp_output_mux_enum),
};

static int cs42l73_spklo_spk_amp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l73_private *priv = snd_soc_component_get_drvdata(component);
	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		/* 150 ms delay between setting PDN and MCLKDIS */
		priv->shutdwn_delay = 150;
		break;
	default:
		pr_err("Invalid event = 0x%x\n", event);
	}
	return 0;
}

static int cs42l73_ear_amp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l73_private *priv = snd_soc_component_get_drvdata(component);
	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		/* 50 ms delay between setting PDN and MCLKDIS */
		if (priv->shutdwn_delay < 50)
			priv->shutdwn_delay = 50;
		break;
	default:
		pr_err("Invalid event = 0x%x\n", event);
	}
	return 0;
}


static int cs42l73_hp_amp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs42l73_private *priv = snd_soc_component_get_drvdata(component);
	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		/* 30 ms delay between setting PDN and MCLKDIS */
		if (priv->shutdwn_delay < 30)
			priv->shutdwn_delay = 30;
		break;
	default:
		pr_err("Invalid event = 0x%x\n", event);
	}
	return 0;
}

static const struct snd_soc_dapm_widget cs42l73_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("DMICA"),
	SND_SOC_DAPM_INPUT("DMICB"),
	SND_SOC_DAPM_INPUT("LINEINA"),
	SND_SOC_DAPM_INPUT("LINEINB"),
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_SUPPLY("MIC1 Bias", CS42L73_PWRCTL2, 6, 1, NULL, 0),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_SUPPLY("MIC2 Bias", CS42L73_PWRCTL2, 7, 1, NULL, 0),

	SND_SOC_DAPM_AIF_OUT("XSPOUTL", NULL,  0,
			CS42L73_PWRCTL2, 1, 1),
	SND_SOC_DAPM_AIF_OUT("XSPOUTR", NULL,  0,
			CS42L73_PWRCTL2, 1, 1),
	SND_SOC_DAPM_AIF_OUT("ASPOUTL", NULL,  0,
			CS42L73_PWRCTL2, 3, 1),
	SND_SOC_DAPM_AIF_OUT("ASPOUTR", NULL,  0,
			CS42L73_PWRCTL2, 3, 1),
	SND_SOC_DAPM_AIF_OUT("VSPINOUT", NULL,  0,
			CS42L73_PWRCTL2, 4, 1),

	SND_SOC_DAPM_PGA("PGA Left", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("PGA Right", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("PGA Left Mux", SND_SOC_NOPM, 0, 0, &pgaa_mux),
	SND_SOC_DAPM_MUX("PGA Right Mux", SND_SOC_NOPM, 0, 0, &pgab_mux),

	SND_SOC_DAPM_ADC("ADC Left", NULL, CS42L73_PWRCTL1, 7, 1),
	SND_SOC_DAPM_ADC("ADC Right", NULL, CS42L73_PWRCTL1, 5, 1),
	SND_SOC_DAPM_ADC("DMIC Left", NULL, CS42L73_PWRCTL1, 6, 1),
	SND_SOC_DAPM_ADC("DMIC Right", NULL, CS42L73_PWRCTL1, 4, 1),

	SND_SOC_DAPM_MIXER_NAMED_CTL("Input Left Capture", SND_SOC_NOPM,
			 0, 0, input_left_mixer,
			 ARRAY_SIZE(input_left_mixer)),

	SND_SOC_DAPM_MIXER_NAMED_CTL("Input Right Capture", SND_SOC_NOPM,
			0, 0, input_right_mixer,
			ARRAY_SIZE(input_right_mixer)),

	SND_SOC_DAPM_MIXER("ASPL Output Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("ASPR Output Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("XSPL Output Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("XSPR Output Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("VSP Output Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_AIF_IN("XSPINL", NULL, 0,
				CS42L73_PWRCTL2, 0, 1),
	SND_SOC_DAPM_AIF_IN("XSPINR", NULL, 0,
				CS42L73_PWRCTL2, 0, 1),
	SND_SOC_DAPM_AIF_IN("XSPINM", NULL, 0,
				CS42L73_PWRCTL2, 0, 1),

	SND_SOC_DAPM_AIF_IN("ASPINL", NULL, 0,
				CS42L73_PWRCTL2, 2, 1),
	SND_SOC_DAPM_AIF_IN("ASPINR", NULL, 0,
				CS42L73_PWRCTL2, 2, 1),
	SND_SOC_DAPM_AIF_IN("ASPINM", NULL, 0,
				CS42L73_PWRCTL2, 2, 1),

	SND_SOC_DAPM_AIF_IN("VSPINOUT", NULL, 0,
				CS42L73_PWRCTL2, 4, 1),

	SND_SOC_DAPM_MIXER("HL Left Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("HL Right Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("SPK Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("ESL Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("ESL-XSP Mux", SND_SOC_NOPM,
			 0, 0, &esl_xsp_mixer),

	SND_SOC_DAPM_MUX("ESL-ASP Mux", SND_SOC_NOPM,
			 0, 0, &esl_asp_mixer),

	SND_SOC_DAPM_MUX("SPK-ASP Mux", SND_SOC_NOPM,
			 0, 0, &spk_asp_mixer),

	SND_SOC_DAPM_MUX("SPK-XSP Mux", SND_SOC_NOPM,
			 0, 0, &spk_xsp_mixer),

	SND_SOC_DAPM_PGA("HL Left DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HL Right DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SPK DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ESL DAC", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SWITCH_E("HP Amp",  CS42L73_PWRCTL3, 0, 1,
			    &hp_amp_ctl, cs42l73_hp_amp_event,
			SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SWITCH("LO Amp", CS42L73_PWRCTL3, 1, 1,
			    &lo_amp_ctl),
	SND_SOC_DAPM_SWITCH_E("SPK Amp", CS42L73_PWRCTL3, 2, 1,
			&spk_amp_ctl, cs42l73_spklo_spk_amp_event,
			SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SWITCH_E("EAR Amp", CS42L73_PWRCTL3, 3, 1,
			    &ear_amp_ctl, cs42l73_ear_amp_event,
			SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SWITCH_E("SPKLO Amp", CS42L73_PWRCTL3, 4, 1,
			    &spklo_amp_ctl, cs42l73_spklo_spk_amp_event,
			SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_OUTPUT("HPOUTA"),
	SND_SOC_DAPM_OUTPUT("HPOUTB"),
	SND_SOC_DAPM_OUTPUT("LINEOUTA"),
	SND_SOC_DAPM_OUTPUT("LINEOUTB"),
	SND_SOC_DAPM_OUTPUT("EAROUT"),
	SND_SOC_DAPM_OUTPUT("SPKOUT"),
	SND_SOC_DAPM_OUTPUT("SPKLINEOUT"),
};

static const struct snd_soc_dapm_route cs42l73_audio_map[] = {

	/* SPKLO EARSPK Paths */
	{"EAROUT", NULL, "EAR Amp"},
	{"SPKLINEOUT", NULL, "SPKLO Amp"},

	{"EAR Amp", "Switch", "ESL DAC"},
	{"SPKLO Amp", "Switch", "ESL DAC"},

	{"ESL DAC", "ESL-ASP Mono Volume", "ESL Mixer"},
	{"ESL DAC", "ESL-XSP Mono Volume", "ESL Mixer"},
	{"ESL DAC", "ESL-VSP Mono Volume", "VSPINOUT"},
	/* Loopback */
	{"ESL DAC", "ESL-IP Mono Volume", "Input Left Capture"},
	{"ESL DAC", "ESL-IP Mono Volume", "Input Right Capture"},

	{"ESL Mixer", NULL, "ESL-ASP Mux"},
	{"ESL Mixer", NULL, "ESL-XSP Mux"},

	{"ESL-ASP Mux", "Left", "ASPINL"},
	{"ESL-ASP Mux", "Right", "ASPINR"},
	{"ESL-ASP Mux", "Mono Mix", "ASPINM"},

	{"ESL-XSP Mux", "Left", "XSPINL"},
	{"ESL-XSP Mux", "Right", "XSPINR"},
	{"ESL-XSP Mux", "Mono Mix", "XSPINM"},

	/* Speakerphone Paths */
	{"SPKOUT", NULL, "SPK Amp"},
	{"SPK Amp", "Switch", "SPK DAC"},

	{"SPK DAC", "SPK-ASP Mono Volume", "SPK Mixer"},
	{"SPK DAC", "SPK-XSP Mono Volume", "SPK Mixer"},
	{"SPK DAC", "SPK-VSP Mono Volume", "VSPINOUT"},
	/* Loopback */
	{"SPK DAC", "SPK-IP Mono Volume", "Input Left Capture"},
	{"SPK DAC", "SPK-IP Mono Volume", "Input Right Capture"},

	{"SPK Mixer", NULL, "SPK-ASP Mux"},
	{"SPK Mixer", NULL, "SPK-XSP Mux"},

	{"SPK-ASP Mux", "Left", "ASPINL"},
	{"SPK-ASP Mux", "Mono Mix", "ASPINM"},
	{"SPK-ASP Mux", "Right", "ASPINR"},

	{"SPK-XSP Mux", "Left", "XSPINL"},
	{"SPK-XSP Mux", "Mono Mix", "XSPINM"},
	{"SPK-XSP Mux", "Right", "XSPINR"},

	/* HP LineOUT Paths */
	{"HPOUTA", NULL, "HP Amp"},
	{"HPOUTB", NULL, "HP Amp"},
	{"LINEOUTA", NULL, "LO Amp"},
	{"LINEOUTB", NULL, "LO Amp"},

	{"HP Amp", "Switch", "HL Left DAC"},
	{"HP Amp", "Switch", "HL Right DAC"},
	{"LO Amp", "Switch", "HL Left DAC"},
	{"LO Amp", "Switch", "HL Right DAC"},

	{"HL Left DAC", "HL-XSP Volume", "HL Left Mixer"},
	{"HL Right DAC", "HL-XSP Volume", "HL Right Mixer"},
	{"HL Left DAC", "HL-ASP Volume", "HL Left Mixer"},
	{"HL Right DAC", "HL-ASP Volume", "HL Right Mixer"},
	{"HL Left DAC", "HL-VSP Volume", "HL Left Mixer"},
	{"HL Right DAC", "HL-VSP Volume", "HL Right Mixer"},
	/* Loopback */
	{"HL Left DAC", "HL-IP Volume", "HL Left Mixer"},
	{"HL Right DAC", "HL-IP Volume", "HL Right Mixer"},
	{"HL Left Mixer", NULL, "Input Left Capture"},
	{"HL Right Mixer", NULL, "Input Right Capture"},

	{"HL Left Mixer", NULL, "ASPINL"},
	{"HL Right Mixer", NULL, "ASPINR"},
	{"HL Left Mixer", NULL, "XSPINL"},
	{"HL Right Mixer", NULL, "XSPINR"},
	{"HL Left Mixer", NULL, "VSPINOUT"},
	{"HL Right Mixer", NULL, "VSPINOUT"},

	{"ASPINL", NULL, "ASP Playback"},
	{"ASPINM", NULL, "ASP Playback"},
	{"ASPINR", NULL, "ASP Playback"},
	{"XSPINL", NULL, "XSP Playback"},
	{"XSPINM", NULL, "XSP Playback"},
	{"XSPINR", NULL, "XSP Playback"},
	{"VSPINOUT", NULL, "VSP Playback"},

	/* Capture Paths */
	{"MIC1", NULL, "MIC1 Bias"},
	{"PGA Left Mux", "Mic 1", "MIC1"},
	{"MIC2", NULL, "MIC2 Bias"},
	{"PGA Right Mux", "Mic 2", "MIC2"},

	{"PGA Left Mux", "Line A", "LINEINA"},
	{"PGA Right Mux", "Line B", "LINEINB"},

	{"PGA Left", NULL, "PGA Left Mux"},
	{"PGA Right", NULL, "PGA Right Mux"},

	{"ADC Left", NULL, "PGA Left"},
	{"ADC Right", NULL, "PGA Right"},
	{"DMIC Left", NULL, "DMICA"},
	{"DMIC Right", NULL, "DMICB"},

	{"Input Left Capture", "ADC Left Input", "ADC Left"},
	{"Input Right Capture", "ADC Right Input", "ADC Right"},
	{"Input Left Capture", "DMIC Left Input", "DMIC Left"},
	{"Input Right Capture", "DMIC Right Input", "DMIC Right"},

	/* Audio Capture */
	{"ASPL Output Mixer", NULL, "Input Left Capture"},
	{"ASPR Output Mixer", NULL, "Input Right Capture"},

	{"ASPOUTL", "ASP-IP Volume", "ASPL Output Mixer"},
	{"ASPOUTR", "ASP-IP Volume", "ASPR Output Mixer"},

	/* Auxillary Capture */
	{"XSPL Output Mixer", NULL, "Input Left Capture"},
	{"XSPR Output Mixer", NULL, "Input Right Capture"},

	{"XSPOUTL", "XSP-IP Volume", "XSPL Output Mixer"},
	{"XSPOUTR", "XSP-IP Volume", "XSPR Output Mixer"},

	{"XSPOUTL", NULL, "XSPL Output Mixer"},
	{"XSPOUTR", NULL, "XSPR Output Mixer"},

	/* Voice Capture */
	{"VSP Output Mixer", NULL, "Input Left Capture"},
	{"VSP Output Mixer", NULL, "Input Right Capture"},

	{"VSPINOUT", "VSP-IP Volume", "VSP Output Mixer"},

	{"VSPINOUT", NULL, "VSP Output Mixer"},

	{"ASP Capture", NULL, "ASPOUTL"},
	{"ASP Capture", NULL, "ASPOUTR"},
	{"XSP Capture", NULL, "XSPOUTL"},
	{"XSP Capture", NULL, "XSPOUTR"},
	{"VSP Capture", NULL, "VSPINOUT"},
};

struct cs42l73_mclk_div {
	u32 mclk;
	u32 srate;
	u8 mmcc;
};

static const struct cs42l73_mclk_div cs42l73_mclk_coeffs[] = {
	/* MCLK, Sample Rate, xMMCC[5:0] */
	{5644800, 11025, 0x30},
	{5644800, 22050, 0x20},
	{5644800, 44100, 0x10},

	{6000000,  8000, 0x39},
	{6000000, 11025, 0x33},
	{6000000, 12000, 0x31},
	{6000000, 16000, 0x29},
	{6000000, 22050, 0x23},
	{6000000, 24000, 0x21},
	{6000000, 32000, 0x19},
	{6000000, 44100, 0x13},
	{6000000, 48000, 0x11},

	{6144000,  8000, 0x38},
	{6144000, 12000, 0x30},
	{6144000, 16000, 0x28},
	{6144000, 24000, 0x20},
	{6144000, 32000, 0x18},
	{6144000, 48000, 0x10},

	{6500000,  8000, 0x3C},
	{6500000, 11025, 0x35},
	{6500000, 12000, 0x34},
	{6500000, 16000, 0x2C},
	{6500000, 22050, 0x25},
	{6500000, 24000, 0x24},
	{6500000, 32000, 0x1C},
	{6500000, 44100, 0x15},
	{6500000, 48000, 0x14},

	{6400000,  8000, 0x3E},
	{6400000, 11025, 0x37},
	{6400000, 12000, 0x36},
	{6400000, 16000, 0x2E},
	{6400000, 22050, 0x27},
	{6400000, 24000, 0x26},
	{6400000, 32000, 0x1E},
	{6400000, 44100, 0x17},
	{6400000, 48000, 0x16},
};

struct cs42l73_mclkx_div {
	u32 mclkx;
	u8 ratio;
	u8 mclkdiv;
};

static const struct cs42l73_mclkx_div cs42l73_mclkx_coeffs[] = {
	{5644800,  1, 0},	/* 5644800 */
	{6000000,  1, 0},	/* 6000000 */
	{6144000,  1, 0},	/* 6144000 */
	{11289600, 2, 2},	/* 5644800 */
	{12288000, 2, 2},	/* 6144000 */
	{12000000, 2, 2},	/* 6000000 */
	{13000000, 2, 2},	/* 6500000 */
	{19200000, 3, 3},	/* 6400000 */
	{24000000, 4, 4},	/* 6000000 */
	{26000000, 4, 4},	/* 6500000 */
	{38400000, 6, 5}	/* 6400000 */
};

static int cs42l73_get_mclkx_coeff(int mclkx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs42l73_mclkx_coeffs); i++) {
		if (cs42l73_mclkx_coeffs[i].mclkx == mclkx)
			return i;
	}
	return -EINVAL;
}

static int cs42l73_get_mclk_coeff(int mclk, int srate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs42l73_mclk_coeffs); i++) {
		if (cs42l73_mclk_coeffs[i].mclk == mclk &&
		    cs42l73_mclk_coeffs[i].srate == srate)
			return i;
	}
	return -EINVAL;

}

static int cs42l73_set_mclk(struct snd_soc_dai *dai, unsigned int freq)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l73_private *priv = snd_soc_component_get_drvdata(component);

	int mclkx_coeff;
	u32 mclk = 0;
	u8 dmmcc = 0;

	/* MCLKX -> MCLK */
	mclkx_coeff = cs42l73_get_mclkx_coeff(freq);
	if (mclkx_coeff < 0)
		return mclkx_coeff;

	mclk = cs42l73_mclkx_coeffs[mclkx_coeff].mclkx /
		cs42l73_mclkx_coeffs[mclkx_coeff].ratio;

	dev_dbg(component->dev, "MCLK%u %u  <-> internal MCLK %u\n",
		 priv->mclksel + 1, cs42l73_mclkx_coeffs[mclkx_coeff].mclkx,
		 mclk);

	dmmcc = (priv->mclksel << 4) |
		(cs42l73_mclkx_coeffs[mclkx_coeff].mclkdiv << 1);

	snd_soc_component_write(component, CS42L73_DMMCC, dmmcc);

	priv->sysclk = mclkx_coeff;
	priv->mclk = mclk;

	return 0;
}

static int cs42l73_set_sysclk(struct snd_soc_dai *dai,
			      int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l73_private *priv = snd_soc_component_get_drvdata(component);

	switch (clk_id) {
	case CS42L73_CLKID_MCLK1:
		break;
	case CS42L73_CLKID_MCLK2:
		break;
	default:
		return -EINVAL;
	}

	if ((cs42l73_set_mclk(dai, freq)) < 0) {
		dev_err(component->dev, "Unable to set MCLK for dai %s\n",
			dai->name);
		return -EINVAL;
	}

	priv->mclksel = clk_id;

	return 0;
}

static int cs42l73_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs42l73_private *priv = snd_soc_component_get_drvdata(component);
	u8 id = codec_dai->id;
	unsigned int inv, format;
	u8 spc, mmcc;

	spc = snd_soc_component_read(component, CS42L73_SPC(id));
	mmcc = snd_soc_component_read(component, CS42L73_MMCC(id));

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		mmcc |= CS42L73_MS_MASTER;
		break;

	case SND_SOC_DAIFMT_CBS_CFS:
		mmcc &= ~CS42L73_MS_MASTER;
		break;

	default:
		return -EINVAL;
	}

	format = (fmt & SND_SOC_DAIFMT_FORMAT_MASK);
	inv = (fmt & SND_SOC_DAIFMT_INV_MASK);

	switch (format) {
	case SND_SOC_DAIFMT_I2S:
		spc &= ~CS42L73_SPDIF_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		if (mmcc & CS42L73_MS_MASTER) {
			dev_err(component->dev,
				"PCM format in slave mode only\n");
			return -EINVAL;
		}
		if (id == CS42L73_ASP) {
			dev_err(component->dev,
				"PCM format is not supported on ASP port\n");
			return -EINVAL;
		}
		spc |= CS42L73_SPDIF_PCM;
		break;
	default:
		return -EINVAL;
	}

	if (spc & CS42L73_SPDIF_PCM) {
		/* Clear PCM mode, clear PCM_BIT_ORDER bit for MSB->LSB */
		spc &= ~(CS42L73_PCM_MODE_MASK | CS42L73_PCM_BIT_ORDER);
		switch (format) {
		case SND_SOC_DAIFMT_DSP_B:
			if (inv == SND_SOC_DAIFMT_IB_IF)
				spc |= CS42L73_PCM_MODE0;
			if (inv == SND_SOC_DAIFMT_IB_NF)
				spc |= CS42L73_PCM_MODE1;
		break;
		case SND_SOC_DAIFMT_DSP_A:
			if (inv == SND_SOC_DAIFMT_IB_IF)
				spc |= CS42L73_PCM_MODE1;
			break;
		default:
			return -EINVAL;
		}
	}

	priv->config[id].spc = spc;
	priv->config[id].mmcc = mmcc;

	return 0;
}

static const unsigned int cs42l73_asrc_rates[] = {
	8000, 11025, 12000, 16000, 22050,
	24000, 32000, 44100, 48000
};

static unsigned int cs42l73_get_xspfs_coeff(u32 rate)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(cs42l73_asrc_rates); i++) {
		if (cs42l73_asrc_rates[i] == rate)
			return i + 1;
	}
	return 0;		/* 0 = Don't know */
}

static void cs42l73_update_asrc(struct snd_soc_component *component, int id, int srate)
{
	u8 spfs = 0;

	if (srate > 0)
		spfs = cs42l73_get_xspfs_coeff(srate);

	switch (id) {
	case CS42L73_XSP:
		snd_soc_component_update_bits(component, CS42L73_VXSPFS, 0x0f, spfs);
	break;
	case CS42L73_ASP:
		snd_soc_component_update_bits(component, CS42L73_ASPC, 0x3c, spfs << 2);
	break;
	case CS42L73_VSP:
		snd_soc_component_update_bits(component, CS42L73_VXSPFS, 0xf0, spfs << 4);
	break;
	default:
	break;
	}
}

static int cs42l73_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l73_private *priv = snd_soc_component_get_drvdata(component);
	int id = dai->id;
	int mclk_coeff;
	int srate = params_rate(params);

	if (priv->config[id].mmcc & CS42L73_MS_MASTER) {
		/* CS42L73 Master */
		/* MCLK -> srate */
		mclk_coeff =
		    cs42l73_get_mclk_coeff(priv->mclk, srate);

		if (mclk_coeff < 0)
			return -EINVAL;

		dev_dbg(component->dev,
			 "DAI[%d]: MCLK %u, srate %u, MMCC[5:0] = %x\n",
			 id, priv->mclk, srate,
			 cs42l73_mclk_coeffs[mclk_coeff].mmcc);

		priv->config[id].mmcc &= 0xC0;
		priv->config[id].mmcc |= cs42l73_mclk_coeffs[mclk_coeff].mmcc;
		priv->config[id].spc &= 0xFC;
		/* Use SCLK=64*Fs if internal MCLK >= 6.4MHz */
		if (priv->mclk >= 6400000)
			priv->config[id].spc |= CS42L73_MCK_SCLK_64FS;
		else
			priv->config[id].spc |= CS42L73_MCK_SCLK_MCLK;
	} else {
		/* CS42L73 Slave */
		priv->config[id].spc &= 0xFC;
		priv->config[id].spc |= CS42L73_MCK_SCLK_64FS;
	}
	/* Update ASRCs */
	priv->config[id].srate = srate;

	snd_soc_component_write(component, CS42L73_SPC(id), priv->config[id].spc);
	snd_soc_component_write(component, CS42L73_MMCC(id), priv->config[id].mmcc);

	cs42l73_update_asrc(component, id, srate);

	return 0;
}

static int cs42l73_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	struct cs42l73_private *cs42l73 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_component_update_bits(component, CS42L73_DMMCC, CS42L73_MCLKDIS, 0);
		snd_soc_component_update_bits(component, CS42L73_PWRCTL1, CS42L73_PDN, 0);
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			regcache_cache_only(cs42l73->regmap, false);
			regcache_sync(cs42l73->regmap);
		}
		snd_soc_component_update_bits(component, CS42L73_PWRCTL1, CS42L73_PDN, 1);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, CS42L73_PWRCTL1, CS42L73_PDN, 1);
		if (cs42l73->shutdwn_delay > 0) {
			mdelay(cs42l73->shutdwn_delay);
			cs42l73->shutdwn_delay = 0;
		} else {
			mdelay(15); /* Min amount of time requred to power
				     * down.
				     */
		}
		snd_soc_component_update_bits(component, CS42L73_DMMCC, CS42L73_MCLKDIS, 1);
		break;
	}
	return 0;
}

static int cs42l73_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct snd_soc_component *component = dai->component;
	int id = dai->id;

	return snd_soc_component_update_bits(component, CS42L73_SPC(id), CS42L73_SP_3ST,
				   tristate << 7);
}

static const struct snd_pcm_hw_constraint_list constraints_12_24 = {
	.count  = ARRAY_SIZE(cs42l73_asrc_rates),
	.list   = cs42l73_asrc_rates,
};

static int cs42l73_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	snd_pcm_hw_constraint_list(substream->runtime, 0,
					SNDRV_PCM_HW_PARAM_RATE,
					&constraints_12_24);
	return 0;
}


#define CS42L73_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops cs42l73_ops = {
	.startup = cs42l73_pcm_startup,
	.hw_params = cs42l73_pcm_hw_params,
	.set_fmt = cs42l73_set_dai_fmt,
	.set_sysclk = cs42l73_set_sysclk,
	.set_tristate = cs42l73_set_tristate,
};

static struct snd_soc_dai_driver cs42l73_dai[] = {
	{
		.name = "cs42l73-xsp",
		.id = CS42L73_XSP,
		.playback = {
			.stream_name = "XSP Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS42L73_FORMATS,
		},
		.capture = {
			.stream_name = "XSP Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS42L73_FORMATS,
		},
		.ops = &cs42l73_ops,
		.symmetric_rate = 1,
	 },
	{
		.name = "cs42l73-asp",
		.id = CS42L73_ASP,
		.playback = {
			.stream_name = "ASP Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS42L73_FORMATS,
		},
		.capture = {
			.stream_name = "ASP Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS42L73_FORMATS,
		},
		.ops = &cs42l73_ops,
		.symmetric_rate = 1,
	 },
	{
		.name = "cs42l73-vsp",
		.id = CS42L73_VSP,
		.playback = {
			.stream_name = "VSP Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS42L73_FORMATS,
		},
		.capture = {
			.stream_name = "VSP Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS42L73_FORMATS,
		},
		.ops = &cs42l73_ops,
		.symmetric_rate = 1,
	 }
};

static int cs42l73_probe(struct snd_soc_component *component)
{
	struct cs42l73_private *cs42l73 = snd_soc_component_get_drvdata(component);

	/* Set Charge Pump Frequency */
	if (cs42l73->pdata.chgfreq)
		snd_soc_component_update_bits(component, CS42L73_CPFCHC,
				    CS42L73_CHARGEPUMP_MASK,
					cs42l73->pdata.chgfreq << 4);

	/* MCLK1 as master clk */
	cs42l73->mclksel = CS42L73_CLKID_MCLK1;
	cs42l73->mclk = 0;

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_cs42l73 = {
	.probe			= cs42l73_probe,
	.set_bias_level		= cs42l73_set_bias_level,
	.controls		= cs42l73_snd_controls,
	.num_controls		= ARRAY_SIZE(cs42l73_snd_controls),
	.dapm_widgets		= cs42l73_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs42l73_dapm_widgets),
	.dapm_routes		= cs42l73_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(cs42l73_audio_map),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config cs42l73_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CS42L73_MAX_REGISTER,
	.reg_defaults = cs42l73_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs42l73_reg_defaults),
	.volatile_reg = cs42l73_volatile_register,
	.readable_reg = cs42l73_readable_register,
	.cache_type = REGCACHE_MAPLE,

	.use_single_read = true,
	.use_single_write = true,
};

static int cs42l73_i2c_probe(struct i2c_client *i2c_client)
{
	struct cs42l73_private *cs42l73;
	struct cs42l73_platform_data *pdata = dev_get_platdata(&i2c_client->dev);
	int ret, devid;
	unsigned int reg;
	u32 val32;

	cs42l73 = devm_kzalloc(&i2c_client->dev, sizeof(*cs42l73), GFP_KERNEL);
	if (!cs42l73)
		return -ENOMEM;

	cs42l73->regmap = devm_regmap_init_i2c(i2c_client, &cs42l73_regmap);
	if (IS_ERR(cs42l73->regmap)) {
		ret = PTR_ERR(cs42l73->regmap);
		dev_err(&i2c_client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	if (pdata) {
		cs42l73->pdata = *pdata;
	} else {
		pdata = devm_kzalloc(&i2c_client->dev, sizeof(*pdata),
				     GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		if (i2c_client->dev.of_node) {
			if (of_property_read_u32(i2c_client->dev.of_node,
				"chgfreq", &val32) >= 0)
				pdata->chgfreq = val32;
		}
		pdata->reset_gpio = of_get_named_gpio(i2c_client->dev.of_node,
						"reset-gpio", 0);
		cs42l73->pdata = *pdata;
	}

	i2c_set_clientdata(i2c_client, cs42l73);

	if (cs42l73->pdata.reset_gpio) {
		ret = devm_gpio_request_one(&i2c_client->dev,
					    cs42l73->pdata.reset_gpio,
					    GPIOF_OUT_INIT_HIGH,
					    "CS42L73 /RST");
		if (ret < 0) {
			dev_err(&i2c_client->dev, "Failed to request /RST %d: %d\n",
				cs42l73->pdata.reset_gpio, ret);
			return ret;
		}
		gpio_set_value_cansleep(cs42l73->pdata.reset_gpio, 0);
		gpio_set_value_cansleep(cs42l73->pdata.reset_gpio, 1);
	}

	/* initialize codec */
	devid = cirrus_read_device_id(cs42l73->regmap, CS42L73_DEVID_AB);
	if (devid < 0) {
		ret = devid;
		dev_err(&i2c_client->dev, "Failed to read device ID: %d\n", ret);
		goto err_reset;
	}

	if (devid != CS42L73_DEVID) {
		ret = -ENODEV;
		dev_err(&i2c_client->dev,
			"CS42L73 Device ID (%X). Expected %X\n",
			devid, CS42L73_DEVID);
		goto err_reset;
	}

	ret = regmap_read(cs42l73->regmap, CS42L73_REVID, &reg);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "Get Revision ID failed\n");
		goto err_reset;
	}

	dev_info(&i2c_client->dev,
		 "Cirrus Logic CS42L73, Revision: %02X\n", reg & 0xFF);

	ret = devm_snd_soc_register_component(&i2c_client->dev,
			&soc_component_dev_cs42l73, cs42l73_dai,
			ARRAY_SIZE(cs42l73_dai));
	if (ret < 0)
		goto err_reset;

	return 0;

err_reset:
	gpio_set_value_cansleep(cs42l73->pdata.reset_gpio, 0);

	return ret;
}

static const struct of_device_id cs42l73_of_match[] = {
	{ .compatible = "cirrus,cs42l73", },
	{},
};
MODULE_DEVICE_TABLE(of, cs42l73_of_match);

static const struct i2c_device_id cs42l73_id[] = {
	{"cs42l73", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, cs42l73_id);

static struct i2c_driver cs42l73_i2c_driver = {
	.driver = {
		   .name = "cs42l73",
		   .of_match_table = cs42l73_of_match,
		   },
	.id_table = cs42l73_id,
	.probe = cs42l73_i2c_probe,

};

module_i2c_driver(cs42l73_i2c_driver);

MODULE_DESCRIPTION("ASoC CS42L73 driver");
MODULE_AUTHOR("Georgi Vlaev, Nucleus Systems Ltd, <joe@nucleusys.com>");
MODULE_AUTHOR("Brian Austin, Cirrus Logic Inc, <brian.austin@cirrus.com>");
MODULE_LICENSE("GPL");
