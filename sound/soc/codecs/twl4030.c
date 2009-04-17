/*
 * ALSA SoC TWL4030 codec driver
 *
 * Author:      Steve Sakoman, <steve@sakoman.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl4030.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "twl4030.h"

/*
 * twl4030 register cache & default register settings
 */
static const u8 twl4030_reg[TWL4030_CACHEREGNUM] = {
	0x00, /* this register not used		*/
	0x91, /* REG_CODEC_MODE		(0x1)	*/
	0xc3, /* REG_OPTION		(0x2)	*/
	0x00, /* REG_UNKNOWN		(0x3)	*/
	0x00, /* REG_MICBIAS_CTL	(0x4)	*/
	0x20, /* REG_ANAMICL		(0x5)	*/
	0x00, /* REG_ANAMICR		(0x6)	*/
	0x00, /* REG_AVADC_CTL		(0x7)	*/
	0x00, /* REG_ADCMICSEL		(0x8)	*/
	0x00, /* REG_DIGMIXING		(0x9)	*/
	0x0c, /* REG_ATXL1PGA		(0xA)	*/
	0x0c, /* REG_ATXR1PGA		(0xB)	*/
	0x00, /* REG_AVTXL2PGA		(0xC)	*/
	0x00, /* REG_AVTXR2PGA		(0xD)	*/
	0x01, /* REG_AUDIO_IF		(0xE)	*/
	0x00, /* REG_VOICE_IF		(0xF)	*/
	0x00, /* REG_ARXR1PGA		(0x10)	*/
	0x00, /* REG_ARXL1PGA		(0x11)	*/
	0x6c, /* REG_ARXR2PGA		(0x12)	*/
	0x6c, /* REG_ARXL2PGA		(0x13)	*/
	0x00, /* REG_VRXPGA		(0x14)	*/
	0x00, /* REG_VSTPGA		(0x15)	*/
	0x00, /* REG_VRX2ARXPGA		(0x16)	*/
	0x0c, /* REG_AVDAC_CTL		(0x17)	*/
	0x00, /* REG_ARX2VTXPGA		(0x18)	*/
	0x00, /* REG_ARXL1_APGA_CTL	(0x19)	*/
	0x00, /* REG_ARXR1_APGA_CTL	(0x1A)	*/
	0x4b, /* REG_ARXL2_APGA_CTL	(0x1B)	*/
	0x4b, /* REG_ARXR2_APGA_CTL	(0x1C)	*/
	0x00, /* REG_ATX2ARXPGA		(0x1D)	*/
	0x00, /* REG_BT_IF		(0x1E)	*/
	0x00, /* REG_BTPGA		(0x1F)	*/
	0x00, /* REG_BTSTPGA		(0x20)	*/
	0x00, /* REG_EAR_CTL		(0x21)	*/
	0x24, /* REG_HS_SEL		(0x22)	*/
	0x0a, /* REG_HS_GAIN_SET	(0x23)	*/
	0x00, /* REG_HS_POPN_SET	(0x24)	*/
	0x00, /* REG_PREDL_CTL		(0x25)	*/
	0x00, /* REG_PREDR_CTL		(0x26)	*/
	0x00, /* REG_PRECKL_CTL		(0x27)	*/
	0x00, /* REG_PRECKR_CTL		(0x28)	*/
	0x00, /* REG_HFL_CTL		(0x29)	*/
	0x00, /* REG_HFR_CTL		(0x2A)	*/
	0x00, /* REG_ALC_CTL		(0x2B)	*/
	0x00, /* REG_ALC_SET1		(0x2C)	*/
	0x00, /* REG_ALC_SET2		(0x2D)	*/
	0x00, /* REG_BOOST_CTL		(0x2E)	*/
	0x00, /* REG_SOFTVOL_CTL	(0x2F)	*/
	0x00, /* REG_DTMF_FREQSEL	(0x30)	*/
	0x00, /* REG_DTMF_TONEXT1H	(0x31)	*/
	0x00, /* REG_DTMF_TONEXT1L	(0x32)	*/
	0x00, /* REG_DTMF_TONEXT2H	(0x33)	*/
	0x00, /* REG_DTMF_TONEXT2L	(0x34)	*/
	0x00, /* REG_DTMF_TONOFF	(0x35)	*/
	0x00, /* REG_DTMF_WANONOFF	(0x36)	*/
	0x00, /* REG_I2S_RX_SCRAMBLE_H	(0x37)	*/
	0x00, /* REG_I2S_RX_SCRAMBLE_M	(0x38)	*/
	0x00, /* REG_I2S_RX_SCRAMBLE_L	(0x39)	*/
	0x16, /* REG_APLL_CTL		(0x3A)	*/
	0x00, /* REG_DTMF_CTL		(0x3B)	*/
	0x00, /* REG_DTMF_PGA_CTL2	(0x3C)	*/
	0x00, /* REG_DTMF_PGA_CTL1	(0x3D)	*/
	0x00, /* REG_MISC_SET_1		(0x3E)	*/
	0x00, /* REG_PCMBTMUX		(0x3F)	*/
	0x00, /* not used		(0x40)	*/
	0x00, /* not used		(0x41)	*/
	0x00, /* not used		(0x42)	*/
	0x00, /* REG_RX_PATH_SEL	(0x43)	*/
	0x00, /* REG_VDL_APGA_CTL	(0x44)	*/
	0x00, /* REG_VIBRA_CTL		(0x45)	*/
	0x00, /* REG_VIBRA_SET		(0x46)	*/
	0x00, /* REG_VIBRA_PWM_SET	(0x47)	*/
	0x00, /* REG_ANAMIC_GAIN	(0x48)	*/
	0x00, /* REG_MISC_SET_2		(0x49)	*/
};

/* codec private data */
struct twl4030_priv {
	unsigned int bypass_state;
	unsigned int codec_powered;
	unsigned int codec_muted;

	struct snd_pcm_substream *master_substream;
	struct snd_pcm_substream *slave_substream;

	unsigned int configured;
	unsigned int rate;
	unsigned int sample_bits;
	unsigned int channels;
};

/*
 * read twl4030 register cache
 */
static inline unsigned int twl4030_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u8 *cache = codec->reg_cache;

	if (reg >= TWL4030_CACHEREGNUM)
		return -EIO;

	return cache[reg];
}

/*
 * write twl4030 register cache
 */
static inline void twl4030_write_reg_cache(struct snd_soc_codec *codec,
						u8 reg, u8 value)
{
	u8 *cache = codec->reg_cache;

	if (reg >= TWL4030_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the twl4030 register space
 */
static int twl4030_write(struct snd_soc_codec *codec,
			unsigned int reg, unsigned int value)
{
	twl4030_write_reg_cache(codec, reg, value);
	return twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE, value, reg);
}

static void twl4030_codec_enable(struct snd_soc_codec *codec, int enable)
{
	struct twl4030_priv *twl4030 = codec->private_data;
	u8 mode;

	if (enable == twl4030->codec_powered)
		return;

	mode = twl4030_read_reg_cache(codec, TWL4030_REG_CODEC_MODE);
	if (enable)
		mode |= TWL4030_CODECPDZ;
	else
		mode &= ~TWL4030_CODECPDZ;

	twl4030_write(codec, TWL4030_REG_CODEC_MODE, mode);
	twl4030->codec_powered = enable;

	/* REVISIT: this delay is present in TI sample drivers */
	/* but there seems to be no TRM requirement for it     */
	udelay(10);
}

static void twl4030_init_chip(struct snd_soc_codec *codec)
{
	int i;

	/* clear CODECPDZ prior to setting register defaults */
	twl4030_codec_enable(codec, 0);

	/* set all audio section registers to reasonable defaults */
	for (i = TWL4030_REG_OPTION; i <= TWL4030_REG_MISC_SET_2; i++)
		twl4030_write(codec, i,	twl4030_reg[i]);

}

static void twl4030_codec_mute(struct snd_soc_codec *codec, int mute)
{
	struct twl4030_priv *twl4030 = codec->private_data;
	u8 reg_val;

	if (mute == twl4030->codec_muted)
		return;

	if (mute) {
		/* Bypass the reg_cache and mute the volumes
		 * Headset mute is done in it's own event handler
		 * Things to mute:  Earpiece, PreDrivL/R, CarkitL/R
		 */
		reg_val = twl4030_read_reg_cache(codec, TWL4030_REG_EAR_CTL);
		twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
					reg_val & (~TWL4030_EAR_GAIN),
					TWL4030_REG_EAR_CTL);

		reg_val = twl4030_read_reg_cache(codec, TWL4030_REG_PREDL_CTL);
		twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
					reg_val & (~TWL4030_PREDL_GAIN),
					TWL4030_REG_PREDL_CTL);
		reg_val = twl4030_read_reg_cache(codec, TWL4030_REG_PREDR_CTL);
		twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
					reg_val & (~TWL4030_PREDR_GAIN),
					TWL4030_REG_PREDL_CTL);

		reg_val = twl4030_read_reg_cache(codec, TWL4030_REG_PRECKL_CTL);
		twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
					reg_val & (~TWL4030_PRECKL_GAIN),
					TWL4030_REG_PRECKL_CTL);
		reg_val = twl4030_read_reg_cache(codec, TWL4030_REG_PRECKR_CTL);
		twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
					reg_val & (~TWL4030_PRECKL_GAIN),
					TWL4030_REG_PRECKR_CTL);

		/* Disable PLL */
		reg_val = twl4030_read_reg_cache(codec, TWL4030_REG_APLL_CTL);
		reg_val &= ~TWL4030_APLL_EN;
		twl4030_write(codec, TWL4030_REG_APLL_CTL, reg_val);
	} else {
		/* Restore the volumes
		 * Headset mute is done in it's own event handler
		 * Things to restore:  Earpiece, PreDrivL/R, CarkitL/R
		 */
		twl4030_write(codec, TWL4030_REG_EAR_CTL,
			twl4030_read_reg_cache(codec, TWL4030_REG_EAR_CTL));

		twl4030_write(codec, TWL4030_REG_PREDL_CTL,
			twl4030_read_reg_cache(codec, TWL4030_REG_PREDL_CTL));
		twl4030_write(codec, TWL4030_REG_PREDR_CTL,
			twl4030_read_reg_cache(codec, TWL4030_REG_PREDR_CTL));

		twl4030_write(codec, TWL4030_REG_PRECKL_CTL,
			twl4030_read_reg_cache(codec, TWL4030_REG_PRECKL_CTL));
		twl4030_write(codec, TWL4030_REG_PRECKR_CTL,
			twl4030_read_reg_cache(codec, TWL4030_REG_PRECKR_CTL));

		/* Enable PLL */
		reg_val = twl4030_read_reg_cache(codec, TWL4030_REG_APLL_CTL);
		reg_val |= TWL4030_APLL_EN;
		twl4030_write(codec, TWL4030_REG_APLL_CTL, reg_val);
	}

	twl4030->codec_muted = mute;
}

static void twl4030_power_up(struct snd_soc_codec *codec)
{
	struct twl4030_priv *twl4030 = codec->private_data;
	u8 anamicl, regmisc1, byte;
	int i = 0;

	if (twl4030->codec_powered)
		return;

	/* set CODECPDZ to turn on codec */
	twl4030_codec_enable(codec, 1);

	/* initiate offset cancellation */
	anamicl = twl4030_read_reg_cache(codec, TWL4030_REG_ANAMICL);
	twl4030_write(codec, TWL4030_REG_ANAMICL,
		anamicl | TWL4030_CNCL_OFFSET_START);

	/* wait for offset cancellation to complete */
	do {
		/* this takes a little while, so don't slam i2c */
		udelay(2000);
		twl4030_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE, &byte,
				    TWL4030_REG_ANAMICL);
	} while ((i++ < 100) &&
		 ((byte & TWL4030_CNCL_OFFSET_START) ==
		  TWL4030_CNCL_OFFSET_START));

	/* Make sure that the reg_cache has the same value as the HW */
	twl4030_write_reg_cache(codec, TWL4030_REG_ANAMICL, byte);

	/* anti-pop when changing analog gain */
	regmisc1 = twl4030_read_reg_cache(codec, TWL4030_REG_MISC_SET_1);
	twl4030_write(codec, TWL4030_REG_MISC_SET_1,
		regmisc1 | TWL4030_SMOOTH_ANAVOL_EN);

	/* toggle CODECPDZ as per TRM */
	twl4030_codec_enable(codec, 0);
	twl4030_codec_enable(codec, 1);
}

/*
 * Unconditional power down
 */
static void twl4030_power_down(struct snd_soc_codec *codec)
{
	/* power down */
	twl4030_codec_enable(codec, 0);
}

/* Earpiece */
static const char *twl4030_earpiece_texts[] =
		{"Off", "DACL1", "DACL2", "DACR1"};

static const unsigned int twl4030_earpiece_values[] =
		{0x0, 0x1, 0x2, 0x4};

static const struct soc_enum twl4030_earpiece_enum =
	SOC_VALUE_ENUM_SINGLE(TWL4030_REG_EAR_CTL, 1, 0x7,
			ARRAY_SIZE(twl4030_earpiece_texts),
			twl4030_earpiece_texts,
			twl4030_earpiece_values);

static const struct snd_kcontrol_new twl4030_dapm_earpiece_control =
SOC_DAPM_VALUE_ENUM("Route", twl4030_earpiece_enum);

/* PreDrive Left */
static const char *twl4030_predrivel_texts[] =
		{"Off", "DACL1", "DACL2", "DACR2"};

static const unsigned int twl4030_predrivel_values[] =
		{0x0, 0x1, 0x2, 0x4};

static const struct soc_enum twl4030_predrivel_enum =
	SOC_VALUE_ENUM_SINGLE(TWL4030_REG_PREDL_CTL, 1, 0x7,
			ARRAY_SIZE(twl4030_predrivel_texts),
			twl4030_predrivel_texts,
			twl4030_predrivel_values);

static const struct snd_kcontrol_new twl4030_dapm_predrivel_control =
SOC_DAPM_VALUE_ENUM("Route", twl4030_predrivel_enum);

/* PreDrive Right */
static const char *twl4030_predriver_texts[] =
		{"Off", "DACR1", "DACR2", "DACL2"};

static const unsigned int twl4030_predriver_values[] =
		{0x0, 0x1, 0x2, 0x4};

static const struct soc_enum twl4030_predriver_enum =
	SOC_VALUE_ENUM_SINGLE(TWL4030_REG_PREDR_CTL, 1, 0x7,
			ARRAY_SIZE(twl4030_predriver_texts),
			twl4030_predriver_texts,
			twl4030_predriver_values);

static const struct snd_kcontrol_new twl4030_dapm_predriver_control =
SOC_DAPM_VALUE_ENUM("Route", twl4030_predriver_enum);

/* Headset Left */
static const char *twl4030_hsol_texts[] =
		{"Off", "DACL1", "DACL2"};

static const struct soc_enum twl4030_hsol_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_HS_SEL, 1,
			ARRAY_SIZE(twl4030_hsol_texts),
			twl4030_hsol_texts);

static const struct snd_kcontrol_new twl4030_dapm_hsol_control =
SOC_DAPM_ENUM("Route", twl4030_hsol_enum);

/* Headset Right */
static const char *twl4030_hsor_texts[] =
		{"Off", "DACR1", "DACR2"};

static const struct soc_enum twl4030_hsor_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_HS_SEL, 4,
			ARRAY_SIZE(twl4030_hsor_texts),
			twl4030_hsor_texts);

static const struct snd_kcontrol_new twl4030_dapm_hsor_control =
SOC_DAPM_ENUM("Route", twl4030_hsor_enum);

/* Carkit Left */
static const char *twl4030_carkitl_texts[] =
		{"Off", "DACL1", "DACL2"};

static const struct soc_enum twl4030_carkitl_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_PRECKL_CTL, 1,
			ARRAY_SIZE(twl4030_carkitl_texts),
			twl4030_carkitl_texts);

static const struct snd_kcontrol_new twl4030_dapm_carkitl_control =
SOC_DAPM_ENUM("Route", twl4030_carkitl_enum);

/* Carkit Right */
static const char *twl4030_carkitr_texts[] =
		{"Off", "DACR1", "DACR2"};

static const struct soc_enum twl4030_carkitr_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_PRECKR_CTL, 1,
			ARRAY_SIZE(twl4030_carkitr_texts),
			twl4030_carkitr_texts);

static const struct snd_kcontrol_new twl4030_dapm_carkitr_control =
SOC_DAPM_ENUM("Route", twl4030_carkitr_enum);

/* Handsfree Left */
static const char *twl4030_handsfreel_texts[] =
		{"Voice", "DACL1", "DACL2", "DACR2"};

static const struct soc_enum twl4030_handsfreel_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_HFL_CTL, 0,
			ARRAY_SIZE(twl4030_handsfreel_texts),
			twl4030_handsfreel_texts);

static const struct snd_kcontrol_new twl4030_dapm_handsfreel_control =
SOC_DAPM_ENUM("Route", twl4030_handsfreel_enum);

/* Handsfree Right */
static const char *twl4030_handsfreer_texts[] =
		{"Voice", "DACR1", "DACR2", "DACL2"};

static const struct soc_enum twl4030_handsfreer_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_HFR_CTL, 0,
			ARRAY_SIZE(twl4030_handsfreer_texts),
			twl4030_handsfreer_texts);

static const struct snd_kcontrol_new twl4030_dapm_handsfreer_control =
SOC_DAPM_ENUM("Route", twl4030_handsfreer_enum);

/* Left analog microphone selection */
static const char *twl4030_analoglmic_texts[] =
		{"Off", "Main mic", "Headset mic", "AUXL", "Carkit mic"};

static const unsigned int twl4030_analoglmic_values[] =
		{0x0, 0x1, 0x2, 0x4, 0x8};

static const struct soc_enum twl4030_analoglmic_enum =
	SOC_VALUE_ENUM_SINGLE(TWL4030_REG_ANAMICL, 0, 0xf,
			ARRAY_SIZE(twl4030_analoglmic_texts),
			twl4030_analoglmic_texts,
			twl4030_analoglmic_values);

static const struct snd_kcontrol_new twl4030_dapm_analoglmic_control =
SOC_DAPM_VALUE_ENUM("Route", twl4030_analoglmic_enum);

/* Right analog microphone selection */
static const char *twl4030_analogrmic_texts[] =
		{"Off", "Sub mic", "AUXR"};

static const unsigned int twl4030_analogrmic_values[] =
		{0x0, 0x1, 0x4};

static const struct soc_enum twl4030_analogrmic_enum =
	SOC_VALUE_ENUM_SINGLE(TWL4030_REG_ANAMICR, 0, 0x5,
			ARRAY_SIZE(twl4030_analogrmic_texts),
			twl4030_analogrmic_texts,
			twl4030_analogrmic_values);

static const struct snd_kcontrol_new twl4030_dapm_analogrmic_control =
SOC_DAPM_VALUE_ENUM("Route", twl4030_analogrmic_enum);

/* TX1 L/R Analog/Digital microphone selection */
static const char *twl4030_micpathtx1_texts[] =
		{"Analog", "Digimic0"};

static const struct soc_enum twl4030_micpathtx1_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_ADCMICSEL, 0,
			ARRAY_SIZE(twl4030_micpathtx1_texts),
			twl4030_micpathtx1_texts);

static const struct snd_kcontrol_new twl4030_dapm_micpathtx1_control =
SOC_DAPM_ENUM("Route", twl4030_micpathtx1_enum);

/* TX2 L/R Analog/Digital microphone selection */
static const char *twl4030_micpathtx2_texts[] =
		{"Analog", "Digimic1"};

static const struct soc_enum twl4030_micpathtx2_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_ADCMICSEL, 2,
			ARRAY_SIZE(twl4030_micpathtx2_texts),
			twl4030_micpathtx2_texts);

static const struct snd_kcontrol_new twl4030_dapm_micpathtx2_control =
SOC_DAPM_ENUM("Route", twl4030_micpathtx2_enum);

/* Analog bypass for AudioR1 */
static const struct snd_kcontrol_new twl4030_dapm_abypassr1_control =
	SOC_DAPM_SINGLE("Switch", TWL4030_REG_ARXR1_APGA_CTL, 2, 1, 0);

/* Analog bypass for AudioL1 */
static const struct snd_kcontrol_new twl4030_dapm_abypassl1_control =
	SOC_DAPM_SINGLE("Switch", TWL4030_REG_ARXL1_APGA_CTL, 2, 1, 0);

/* Analog bypass for AudioR2 */
static const struct snd_kcontrol_new twl4030_dapm_abypassr2_control =
	SOC_DAPM_SINGLE("Switch", TWL4030_REG_ARXR2_APGA_CTL, 2, 1, 0);

/* Analog bypass for AudioL2 */
static const struct snd_kcontrol_new twl4030_dapm_abypassl2_control =
	SOC_DAPM_SINGLE("Switch", TWL4030_REG_ARXL2_APGA_CTL, 2, 1, 0);

/* Digital bypass gain, 0 mutes the bypass */
static const unsigned int twl4030_dapm_dbypass_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 3, TLV_DB_SCALE_ITEM(-2400, 0, 1),
	4, 7, TLV_DB_SCALE_ITEM(-1800, 600, 0),
};

/* Digital bypass left (TX1L -> RX2L) */
static const struct snd_kcontrol_new twl4030_dapm_dbypassl_control =
	SOC_DAPM_SINGLE_TLV("Volume",
			TWL4030_REG_ATX2ARXPGA, 3, 7, 0,
			twl4030_dapm_dbypass_tlv);

/* Digital bypass right (TX1R -> RX2R) */
static const struct snd_kcontrol_new twl4030_dapm_dbypassr_control =
	SOC_DAPM_SINGLE_TLV("Volume",
			TWL4030_REG_ATX2ARXPGA, 0, 7, 0,
			twl4030_dapm_dbypass_tlv);

static int micpath_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct soc_enum *e = (struct soc_enum *)w->kcontrols->private_value;
	unsigned char adcmicsel, micbias_ctl;

	adcmicsel = twl4030_read_reg_cache(w->codec, TWL4030_REG_ADCMICSEL);
	micbias_ctl = twl4030_read_reg_cache(w->codec, TWL4030_REG_MICBIAS_CTL);
	/* Prepare the bits for the given TX path:
	 * shift_l == 0: TX1 microphone path
	 * shift_l == 2: TX2 microphone path */
	if (e->shift_l) {
		/* TX2 microphone path */
		if (adcmicsel & TWL4030_TX2IN_SEL)
			micbias_ctl |= TWL4030_MICBIAS2_CTL; /* digimic */
		else
			micbias_ctl &= ~TWL4030_MICBIAS2_CTL;
	} else {
		/* TX1 microphone path */
		if (adcmicsel & TWL4030_TX1IN_SEL)
			micbias_ctl |= TWL4030_MICBIAS1_CTL; /* digimic */
		else
			micbias_ctl &= ~TWL4030_MICBIAS1_CTL;
	}

	twl4030_write(w->codec, TWL4030_REG_MICBIAS_CTL, micbias_ctl);

	return 0;
}

static int handsfree_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct soc_enum *e = (struct soc_enum *)w->kcontrols->private_value;
	unsigned char hs_ctl;

	hs_ctl = twl4030_read_reg_cache(w->codec, e->reg);

	if (hs_ctl & TWL4030_HF_CTL_REF_EN) {
		hs_ctl |= TWL4030_HF_CTL_RAMP_EN;
		twl4030_write(w->codec, e->reg, hs_ctl);
		hs_ctl |= TWL4030_HF_CTL_LOOP_EN;
		twl4030_write(w->codec, e->reg, hs_ctl);
		hs_ctl |= TWL4030_HF_CTL_HB_EN;
		twl4030_write(w->codec, e->reg, hs_ctl);
	} else {
		hs_ctl &= ~(TWL4030_HF_CTL_RAMP_EN | TWL4030_HF_CTL_LOOP_EN
				| TWL4030_HF_CTL_HB_EN);
		twl4030_write(w->codec, e->reg, hs_ctl);
	}

	return 0;
}

static int headsetl_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	unsigned char hs_gain, hs_pop;

	/* Save the current volume */
	hs_gain = twl4030_read_reg_cache(w->codec, TWL4030_REG_HS_GAIN_SET);
	hs_pop = twl4030_read_reg_cache(w->codec, TWL4030_REG_HS_POPN_SET);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* Do the anti-pop/bias ramp enable according to the TRM */
		hs_pop |= TWL4030_VMID_EN;
		twl4030_write(w->codec, TWL4030_REG_HS_POPN_SET, hs_pop);
		/* Is this needed? Can we just use whatever gain here? */
		twl4030_write(w->codec, TWL4030_REG_HS_GAIN_SET,
				(hs_gain & (~0x0f)) | 0x0a);
		hs_pop |= TWL4030_RAMP_EN;
		twl4030_write(w->codec, TWL4030_REG_HS_POPN_SET, hs_pop);

		/* Restore the original volume */
		twl4030_write(w->codec, TWL4030_REG_HS_GAIN_SET, hs_gain);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Do the anti-pop/bias ramp disable according to the TRM */
		hs_pop &= ~TWL4030_RAMP_EN;
		twl4030_write(w->codec, TWL4030_REG_HS_POPN_SET, hs_pop);
		/* Bypass the reg_cache to mute the headset */
		twl4030_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
					hs_gain & (~0x0f),
					TWL4030_REG_HS_GAIN_SET);
		hs_pop &= ~TWL4030_VMID_EN;
		twl4030_write(w->codec, TWL4030_REG_HS_POPN_SET, hs_pop);
		break;
	}
	return 0;
}

static int bypass_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct soc_mixer_control *m =
		(struct soc_mixer_control *)w->kcontrols->private_value;
	struct twl4030_priv *twl4030 = w->codec->private_data;
	unsigned char reg;

	reg = twl4030_read_reg_cache(w->codec, m->reg);

	if (m->reg <= TWL4030_REG_ARXR2_APGA_CTL) {
		/* Analog bypass */
		if (reg & (1 << m->shift))
			twl4030->bypass_state |=
				(1 << (m->reg - TWL4030_REG_ARXL1_APGA_CTL));
		else
			twl4030->bypass_state &=
				~(1 << (m->reg - TWL4030_REG_ARXL1_APGA_CTL));
	} else {
		/* Digital bypass */
		if (reg & (0x7 << m->shift))
			twl4030->bypass_state |= (1 << (m->shift ? 5 : 4));
		else
			twl4030->bypass_state &= ~(1 << (m->shift ? 5 : 4));
	}

	if (w->codec->bias_level == SND_SOC_BIAS_STANDBY) {
		if (twl4030->bypass_state)
			twl4030_codec_mute(w->codec, 0);
		else
			twl4030_codec_mute(w->codec, 1);
	}
	return 0;
}

/*
 * Some of the gain controls in TWL (mostly those which are associated with
 * the outputs) are implemented in an interesting way:
 * 0x0 : Power down (mute)
 * 0x1 : 6dB
 * 0x2 : 0 dB
 * 0x3 : -6 dB
 * Inverting not going to help with these.
 * Custom volsw and volsw_2r get/put functions to handle these gain bits.
 */
#define SOC_DOUBLE_TLV_TWL4030(xname, xreg, shift_left, shift_right, xmax,\
			       xinvert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_get_volsw_twl4030, \
	.put = snd_soc_put_volsw_twl4030, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = xreg, .shift = shift_left, .rshift = shift_right,\
		 .max = xmax, .invert = xinvert} }
#define SOC_DOUBLE_R_TLV_TWL4030(xname, reg_left, reg_right, xshift, xmax,\
				 xinvert, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw_2r, \
	.get = snd_soc_get_volsw_r2_twl4030,\
	.put = snd_soc_put_volsw_r2_twl4030, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = reg_left, .rreg = reg_right, .shift = xshift, \
		 .rshift = xshift, .max = xmax, .invert = xinvert} }
#define SOC_SINGLE_TLV_TWL4030(xname, xreg, xshift, xmax, xinvert, tlv_array) \
	SOC_DOUBLE_TLV_TWL4030(xname, xreg, xshift, xshift, xmax, \
			       xinvert, tlv_array)

static int snd_soc_get_volsw_twl4030(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	int mask = (1 << fls(max)) - 1;

	ucontrol->value.integer.value[0] =
		(snd_soc_read(codec, reg) >> shift) & mask;
	if (ucontrol->value.integer.value[0])
		ucontrol->value.integer.value[0] =
			max + 1 - ucontrol->value.integer.value[0];

	if (shift != rshift) {
		ucontrol->value.integer.value[1] =
			(snd_soc_read(codec, reg) >> rshift) & mask;
		if (ucontrol->value.integer.value[1])
			ucontrol->value.integer.value[1] =
				max + 1 - ucontrol->value.integer.value[1];
	}

	return 0;
}

static int snd_soc_put_volsw_twl4030(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	int mask = (1 << fls(max)) - 1;
	unsigned short val, val2, val_mask;

	val = (ucontrol->value.integer.value[0] & mask);

	val_mask = mask << shift;
	if (val)
		val = max + 1 - val;
	val = val << shift;
	if (shift != rshift) {
		val2 = (ucontrol->value.integer.value[1] & mask);
		val_mask |= mask << rshift;
		if (val2)
			val2 = max + 1 - val2;
		val |= val2 << rshift;
	}
	return snd_soc_update_bits(codec, reg, val_mask, val);
}

static int snd_soc_get_volsw_r2_twl4030(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	int mask = (1<<fls(max))-1;

	ucontrol->value.integer.value[0] =
		(snd_soc_read(codec, reg) >> shift) & mask;
	ucontrol->value.integer.value[1] =
		(snd_soc_read(codec, reg2) >> shift) & mask;

	if (ucontrol->value.integer.value[0])
		ucontrol->value.integer.value[0] =
			max + 1 - ucontrol->value.integer.value[0];
	if (ucontrol->value.integer.value[1])
		ucontrol->value.integer.value[1] =
			max + 1 - ucontrol->value.integer.value[1];

	return 0;
}

static int snd_soc_put_volsw_r2_twl4030(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	int mask = (1 << fls(max)) - 1;
	int err;
	unsigned short val, val2, val_mask;

	val_mask = mask << shift;
	val = (ucontrol->value.integer.value[0] & mask);
	val2 = (ucontrol->value.integer.value[1] & mask);

	if (val)
		val = max + 1 - val;
	if (val2)
		val2 = max + 1 - val2;

	val = val << shift;
	val2 = val2 << shift;

	err = snd_soc_update_bits(codec, reg, val_mask, val);
	if (err < 0)
		return err;

	err = snd_soc_update_bits(codec, reg2, val_mask, val2);
	return err;
}

/*
 * FGAIN volume control:
 * from -62 to 0 dB in 1 dB steps (mute instead of -63 dB)
 */
static DECLARE_TLV_DB_SCALE(digital_fine_tlv, -6300, 100, 1);

/*
 * CGAIN volume control:
 * 0 dB to 12 dB in 6 dB steps
 * value 2 and 3 means 12 dB
 */
static DECLARE_TLV_DB_SCALE(digital_coarse_tlv, 0, 600, 0);

/*
 * Analog playback gain
 * -24 dB to 12 dB in 2 dB steps
 */
static DECLARE_TLV_DB_SCALE(analog_tlv, -2400, 200, 0);

/*
 * Gain controls tied to outputs
 * -6 dB to 6 dB in 6 dB steps (mute instead of -12)
 */
static DECLARE_TLV_DB_SCALE(output_tvl, -1200, 600, 1);

/*
 * Capture gain after the ADCs
 * from 0 dB to 31 dB in 1 dB steps
 */
static DECLARE_TLV_DB_SCALE(digital_capture_tlv, 0, 100, 0);

/*
 * Gain control for input amplifiers
 * 0 dB to 30 dB in 6 dB steps
 */
static DECLARE_TLV_DB_SCALE(input_gain_tlv, 0, 600, 0);

static const char *twl4030_rampdelay_texts[] = {
	"27/20/14 ms", "55/40/27 ms", "109/81/55 ms", "218/161/109 ms",
	"437/323/218 ms", "874/645/437 ms", "1748/1291/874 ms",
	"3495/2581/1748 ms"
};

static const struct soc_enum twl4030_rampdelay_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_HS_POPN_SET, 2,
			ARRAY_SIZE(twl4030_rampdelay_texts),
			twl4030_rampdelay_texts);

static const struct snd_kcontrol_new twl4030_snd_controls[] = {
	/* Common playback gain controls */
	SOC_DOUBLE_R_TLV("DAC1 Digital Fine Playback Volume",
		TWL4030_REG_ARXL1PGA, TWL4030_REG_ARXR1PGA,
		0, 0x3f, 0, digital_fine_tlv),
	SOC_DOUBLE_R_TLV("DAC2 Digital Fine Playback Volume",
		TWL4030_REG_ARXL2PGA, TWL4030_REG_ARXR2PGA,
		0, 0x3f, 0, digital_fine_tlv),

	SOC_DOUBLE_R_TLV("DAC1 Digital Coarse Playback Volume",
		TWL4030_REG_ARXL1PGA, TWL4030_REG_ARXR1PGA,
		6, 0x2, 0, digital_coarse_tlv),
	SOC_DOUBLE_R_TLV("DAC2 Digital Coarse Playback Volume",
		TWL4030_REG_ARXL2PGA, TWL4030_REG_ARXR2PGA,
		6, 0x2, 0, digital_coarse_tlv),

	SOC_DOUBLE_R_TLV("DAC1 Analog Playback Volume",
		TWL4030_REG_ARXL1_APGA_CTL, TWL4030_REG_ARXR1_APGA_CTL,
		3, 0x12, 1, analog_tlv),
	SOC_DOUBLE_R_TLV("DAC2 Analog Playback Volume",
		TWL4030_REG_ARXL2_APGA_CTL, TWL4030_REG_ARXR2_APGA_CTL,
		3, 0x12, 1, analog_tlv),
	SOC_DOUBLE_R("DAC1 Analog Playback Switch",
		TWL4030_REG_ARXL1_APGA_CTL, TWL4030_REG_ARXR1_APGA_CTL,
		1, 1, 0),
	SOC_DOUBLE_R("DAC2 Analog Playback Switch",
		TWL4030_REG_ARXL2_APGA_CTL, TWL4030_REG_ARXR2_APGA_CTL,
		1, 1, 0),

	/* Separate output gain controls */
	SOC_DOUBLE_R_TLV_TWL4030("PreDriv Playback Volume",
		TWL4030_REG_PREDL_CTL, TWL4030_REG_PREDR_CTL,
		4, 3, 0, output_tvl),

	SOC_DOUBLE_TLV_TWL4030("Headset Playback Volume",
		TWL4030_REG_HS_GAIN_SET, 0, 2, 3, 0, output_tvl),

	SOC_DOUBLE_R_TLV_TWL4030("Carkit Playback Volume",
		TWL4030_REG_PRECKL_CTL, TWL4030_REG_PRECKR_CTL,
		4, 3, 0, output_tvl),

	SOC_SINGLE_TLV_TWL4030("Earpiece Playback Volume",
		TWL4030_REG_EAR_CTL, 4, 3, 0, output_tvl),

	/* Common capture gain controls */
	SOC_DOUBLE_R_TLV("TX1 Digital Capture Volume",
		TWL4030_REG_ATXL1PGA, TWL4030_REG_ATXR1PGA,
		0, 0x1f, 0, digital_capture_tlv),
	SOC_DOUBLE_R_TLV("TX2 Digital Capture Volume",
		TWL4030_REG_AVTXL2PGA, TWL4030_REG_AVTXR2PGA,
		0, 0x1f, 0, digital_capture_tlv),

	SOC_DOUBLE_TLV("Analog Capture Volume", TWL4030_REG_ANAMIC_GAIN,
		0, 3, 5, 0, input_gain_tlv),

	SOC_ENUM("HS ramp delay", twl4030_rampdelay_enum),
};

static const struct snd_soc_dapm_widget twl4030_dapm_widgets[] = {
	/* Left channel inputs */
	SND_SOC_DAPM_INPUT("MAINMIC"),
	SND_SOC_DAPM_INPUT("HSMIC"),
	SND_SOC_DAPM_INPUT("AUXL"),
	SND_SOC_DAPM_INPUT("CARKITMIC"),
	/* Right channel inputs */
	SND_SOC_DAPM_INPUT("SUBMIC"),
	SND_SOC_DAPM_INPUT("AUXR"),
	/* Digital microphones (Stereo) */
	SND_SOC_DAPM_INPUT("DIGIMIC0"),
	SND_SOC_DAPM_INPUT("DIGIMIC1"),

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("OUTL"),
	SND_SOC_DAPM_OUTPUT("OUTR"),
	SND_SOC_DAPM_OUTPUT("EARPIECE"),
	SND_SOC_DAPM_OUTPUT("PREDRIVEL"),
	SND_SOC_DAPM_OUTPUT("PREDRIVER"),
	SND_SOC_DAPM_OUTPUT("HSOL"),
	SND_SOC_DAPM_OUTPUT("HSOR"),
	SND_SOC_DAPM_OUTPUT("CARKITL"),
	SND_SOC_DAPM_OUTPUT("CARKITR"),
	SND_SOC_DAPM_OUTPUT("HFL"),
	SND_SOC_DAPM_OUTPUT("HFR"),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC Right1", "Right Front Playback",
			SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC Left1", "Left Front Playback",
			SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC Right2", "Right Rear Playback",
			SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC Left2", "Left Rear Playback",
			SND_SOC_NOPM, 0, 0),

	/* Analog PGAs */
	SND_SOC_DAPM_PGA("ARXR1_APGA", TWL4030_REG_ARXR1_APGA_CTL,
			0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ARXL1_APGA", TWL4030_REG_ARXL1_APGA_CTL,
			0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ARXR2_APGA", TWL4030_REG_ARXR2_APGA_CTL,
			0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ARXL2_APGA", TWL4030_REG_ARXL2_APGA_CTL,
			0, 0, NULL, 0),

	/* Analog bypasses */
	SND_SOC_DAPM_SWITCH_E("Right1 Analog Loopback", SND_SOC_NOPM, 0, 0,
			&twl4030_dapm_abypassr1_control, bypass_event,
			SND_SOC_DAPM_POST_REG),
	SND_SOC_DAPM_SWITCH_E("Left1 Analog Loopback", SND_SOC_NOPM, 0, 0,
			&twl4030_dapm_abypassl1_control,
			bypass_event, SND_SOC_DAPM_POST_REG),
	SND_SOC_DAPM_SWITCH_E("Right2 Analog Loopback", SND_SOC_NOPM, 0, 0,
			&twl4030_dapm_abypassr2_control,
			bypass_event, SND_SOC_DAPM_POST_REG),
	SND_SOC_DAPM_SWITCH_E("Left2 Analog Loopback", SND_SOC_NOPM, 0, 0,
			&twl4030_dapm_abypassl2_control,
			bypass_event, SND_SOC_DAPM_POST_REG),

	/* Digital bypasses */
	SND_SOC_DAPM_SWITCH_E("Left Digital Loopback", SND_SOC_NOPM, 0, 0,
			&twl4030_dapm_dbypassl_control, bypass_event,
			SND_SOC_DAPM_POST_REG),
	SND_SOC_DAPM_SWITCH_E("Right Digital Loopback", SND_SOC_NOPM, 0, 0,
			&twl4030_dapm_dbypassr_control, bypass_event,
			SND_SOC_DAPM_POST_REG),

	SND_SOC_DAPM_MIXER("Analog R1 Playback Mixer", TWL4030_REG_AVDAC_CTL,
			0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Analog L1 Playback Mixer", TWL4030_REG_AVDAC_CTL,
			1, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Analog R2 Playback Mixer", TWL4030_REG_AVDAC_CTL,
			2, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Analog L2 Playback Mixer", TWL4030_REG_AVDAC_CTL,
			3, 0, NULL, 0),

	/* Output MUX controls */
	/* Earpiece */
	SND_SOC_DAPM_VALUE_MUX("Earpiece Mux", SND_SOC_NOPM, 0, 0,
		&twl4030_dapm_earpiece_control),
	/* PreDrivL/R */
	SND_SOC_DAPM_VALUE_MUX("PredriveL Mux", SND_SOC_NOPM, 0, 0,
		&twl4030_dapm_predrivel_control),
	SND_SOC_DAPM_VALUE_MUX("PredriveR Mux", SND_SOC_NOPM, 0, 0,
		&twl4030_dapm_predriver_control),
	/* HeadsetL/R */
	SND_SOC_DAPM_MUX_E("HeadsetL Mux", SND_SOC_NOPM, 0, 0,
		&twl4030_dapm_hsol_control, headsetl_event,
		SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX("HeadsetR Mux", SND_SOC_NOPM, 0, 0,
		&twl4030_dapm_hsor_control),
	/* CarkitL/R */
	SND_SOC_DAPM_MUX("CarkitL Mux", SND_SOC_NOPM, 0, 0,
		&twl4030_dapm_carkitl_control),
	SND_SOC_DAPM_MUX("CarkitR Mux", SND_SOC_NOPM, 0, 0,
		&twl4030_dapm_carkitr_control),
	/* HandsfreeL/R */
	SND_SOC_DAPM_MUX_E("HandsfreeL Mux", TWL4030_REG_HFL_CTL, 5, 0,
		&twl4030_dapm_handsfreel_control, handsfree_event,
		SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("HandsfreeR Mux", TWL4030_REG_HFR_CTL, 5, 0,
		&twl4030_dapm_handsfreer_control, handsfree_event,
		SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),

	/* Introducing four virtual ADC, since TWL4030 have four channel for
	   capture */
	SND_SOC_DAPM_ADC("ADC Virtual Left1", "Left Front Capture",
		SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC Virtual Right1", "Right Front Capture",
		SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC Virtual Left2", "Left Rear Capture",
		SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC Virtual Right2", "Right Rear Capture",
		SND_SOC_NOPM, 0, 0),

	/* Analog/Digital mic path selection.
	   TX1 Left/Right: either analog Left/Right or Digimic0
	   TX2 Left/Right: either analog Left/Right or Digimic1 */
	SND_SOC_DAPM_MUX_E("TX1 Capture Route", SND_SOC_NOPM, 0, 0,
		&twl4030_dapm_micpathtx1_control, micpath_event,
		SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD|
		SND_SOC_DAPM_POST_REG),
	SND_SOC_DAPM_MUX_E("TX2 Capture Route", SND_SOC_NOPM, 0, 0,
		&twl4030_dapm_micpathtx2_control, micpath_event,
		SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD|
		SND_SOC_DAPM_POST_REG),

	/* Analog input muxes with switch for the capture amplifiers */
	SND_SOC_DAPM_VALUE_MUX("Analog Left Capture Route",
		TWL4030_REG_ANAMICL, 4, 0, &twl4030_dapm_analoglmic_control),
	SND_SOC_DAPM_VALUE_MUX("Analog Right Capture Route",
		TWL4030_REG_ANAMICR, 4, 0, &twl4030_dapm_analogrmic_control),

	SND_SOC_DAPM_PGA("ADC Physical Left",
		TWL4030_REG_AVADC_CTL, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ADC Physical Right",
		TWL4030_REG_AVADC_CTL, 1, 0, NULL, 0),

	SND_SOC_DAPM_PGA("Digimic0 Enable",
		TWL4030_REG_ADCMICSEL, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Digimic1 Enable",
		TWL4030_REG_ADCMICSEL, 3, 0, NULL, 0),

	SND_SOC_DAPM_MICBIAS("Mic Bias 1", TWL4030_REG_MICBIAS_CTL, 0, 0),
	SND_SOC_DAPM_MICBIAS("Mic Bias 2", TWL4030_REG_MICBIAS_CTL, 1, 0),
	SND_SOC_DAPM_MICBIAS("Headset Mic Bias", TWL4030_REG_MICBIAS_CTL, 2, 0),

};

static const struct snd_soc_dapm_route intercon[] = {
	{"Analog L1 Playback Mixer", NULL, "DAC Left1"},
	{"Analog R1 Playback Mixer", NULL, "DAC Right1"},
	{"Analog L2 Playback Mixer", NULL, "DAC Left2"},
	{"Analog R2 Playback Mixer", NULL, "DAC Right2"},

	{"ARXL1_APGA", NULL, "Analog L1 Playback Mixer"},
	{"ARXR1_APGA", NULL, "Analog R1 Playback Mixer"},
	{"ARXL2_APGA", NULL, "Analog L2 Playback Mixer"},
	{"ARXR2_APGA", NULL, "Analog R2 Playback Mixer"},

	/* Internal playback routings */
	/* Earpiece */
	{"Earpiece Mux", "DACL1", "ARXL1_APGA"},
	{"Earpiece Mux", "DACL2", "ARXL2_APGA"},
	{"Earpiece Mux", "DACR1", "ARXR1_APGA"},
	/* PreDrivL */
	{"PredriveL Mux", "DACL1", "ARXL1_APGA"},
	{"PredriveL Mux", "DACL2", "ARXL2_APGA"},
	{"PredriveL Mux", "DACR2", "ARXR2_APGA"},
	/* PreDrivR */
	{"PredriveR Mux", "DACR1", "ARXR1_APGA"},
	{"PredriveR Mux", "DACR2", "ARXR2_APGA"},
	{"PredriveR Mux", "DACL2", "ARXL2_APGA"},
	/* HeadsetL */
	{"HeadsetL Mux", "DACL1", "ARXL1_APGA"},
	{"HeadsetL Mux", "DACL2", "ARXL2_APGA"},
	/* HeadsetR */
	{"HeadsetR Mux", "DACR1", "ARXR1_APGA"},
	{"HeadsetR Mux", "DACR2", "ARXR2_APGA"},
	/* CarkitL */
	{"CarkitL Mux", "DACL1", "ARXL1_APGA"},
	{"CarkitL Mux", "DACL2", "ARXL2_APGA"},
	/* CarkitR */
	{"CarkitR Mux", "DACR1", "ARXR1_APGA"},
	{"CarkitR Mux", "DACR2", "ARXR2_APGA"},
	/* HandsfreeL */
	{"HandsfreeL Mux", "DACL1", "ARXL1_APGA"},
	{"HandsfreeL Mux", "DACL2", "ARXL2_APGA"},
	{"HandsfreeL Mux", "DACR2", "ARXR2_APGA"},
	/* HandsfreeR */
	{"HandsfreeR Mux", "DACR1", "ARXR1_APGA"},
	{"HandsfreeR Mux", "DACR2", "ARXR2_APGA"},
	{"HandsfreeR Mux", "DACL2", "ARXL2_APGA"},

	/* outputs */
	{"OUTL", NULL, "ARXL2_APGA"},
	{"OUTR", NULL, "ARXR2_APGA"},
	{"EARPIECE", NULL, "Earpiece Mux"},
	{"PREDRIVEL", NULL, "PredriveL Mux"},
	{"PREDRIVER", NULL, "PredriveR Mux"},
	{"HSOL", NULL, "HeadsetL Mux"},
	{"HSOR", NULL, "HeadsetR Mux"},
	{"CARKITL", NULL, "CarkitL Mux"},
	{"CARKITR", NULL, "CarkitR Mux"},
	{"HFL", NULL, "HandsfreeL Mux"},
	{"HFR", NULL, "HandsfreeR Mux"},

	/* Capture path */
	{"Analog Left Capture Route", "Main mic", "MAINMIC"},
	{"Analog Left Capture Route", "Headset mic", "HSMIC"},
	{"Analog Left Capture Route", "AUXL", "AUXL"},
	{"Analog Left Capture Route", "Carkit mic", "CARKITMIC"},

	{"Analog Right Capture Route", "Sub mic", "SUBMIC"},
	{"Analog Right Capture Route", "AUXR", "AUXR"},

	{"ADC Physical Left", NULL, "Analog Left Capture Route"},
	{"ADC Physical Right", NULL, "Analog Right Capture Route"},

	{"Digimic0 Enable", NULL, "DIGIMIC0"},
	{"Digimic1 Enable", NULL, "DIGIMIC1"},

	/* TX1 Left capture path */
	{"TX1 Capture Route", "Analog", "ADC Physical Left"},
	{"TX1 Capture Route", "Digimic0", "Digimic0 Enable"},
	/* TX1 Right capture path */
	{"TX1 Capture Route", "Analog", "ADC Physical Right"},
	{"TX1 Capture Route", "Digimic0", "Digimic0 Enable"},
	/* TX2 Left capture path */
	{"TX2 Capture Route", "Analog", "ADC Physical Left"},
	{"TX2 Capture Route", "Digimic1", "Digimic1 Enable"},
	/* TX2 Right capture path */
	{"TX2 Capture Route", "Analog", "ADC Physical Right"},
	{"TX2 Capture Route", "Digimic1", "Digimic1 Enable"},

	{"ADC Virtual Left1", NULL, "TX1 Capture Route"},
	{"ADC Virtual Right1", NULL, "TX1 Capture Route"},
	{"ADC Virtual Left2", NULL, "TX2 Capture Route"},
	{"ADC Virtual Right2", NULL, "TX2 Capture Route"},

	/* Analog bypass routes */
	{"Right1 Analog Loopback", "Switch", "Analog Right Capture Route"},
	{"Left1 Analog Loopback", "Switch", "Analog Left Capture Route"},
	{"Right2 Analog Loopback", "Switch", "Analog Right Capture Route"},
	{"Left2 Analog Loopback", "Switch", "Analog Left Capture Route"},

	{"Analog R1 Playback Mixer", NULL, "Right1 Analog Loopback"},
	{"Analog L1 Playback Mixer", NULL, "Left1 Analog Loopback"},
	{"Analog R2 Playback Mixer", NULL, "Right2 Analog Loopback"},
	{"Analog L2 Playback Mixer", NULL, "Left2 Analog Loopback"},

	/* Digital bypass routes */
	{"Right Digital Loopback", "Volume", "TX1 Capture Route"},
	{"Left Digital Loopback", "Volume", "TX1 Capture Route"},

	{"Analog R2 Playback Mixer", NULL, "Right Digital Loopback"},
	{"Analog L2 Playback Mixer", NULL, "Left Digital Loopback"},

};

static int twl4030_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, twl4030_dapm_widgets,
				 ARRAY_SIZE(twl4030_dapm_widgets));

	snd_soc_dapm_add_routes(codec, intercon, ARRAY_SIZE(intercon));

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

static int twl4030_set_bias_level(struct snd_soc_codec *codec,
				  enum snd_soc_bias_level level)
{
	struct twl4030_priv *twl4030 = codec->private_data;

	switch (level) {
	case SND_SOC_BIAS_ON:
		twl4030_codec_mute(codec, 0);
		break;
	case SND_SOC_BIAS_PREPARE:
		twl4030_power_up(codec);
		if (twl4030->bypass_state)
			twl4030_codec_mute(codec, 0);
		else
			twl4030_codec_mute(codec, 1);
		break;
	case SND_SOC_BIAS_STANDBY:
		twl4030_power_up(codec);
		if (twl4030->bypass_state)
			twl4030_codec_mute(codec, 0);
		else
			twl4030_codec_mute(codec, 1);
		break;
	case SND_SOC_BIAS_OFF:
		twl4030_power_down(codec);
		break;
	}
	codec->bias_level = level;

	return 0;
}

static void twl4030_constraints(struct twl4030_priv *twl4030,
				struct snd_pcm_substream *mst_substream)
{
	struct snd_pcm_substream *slv_substream;

	/* Pick the stream, which need to be constrained */
	if (mst_substream == twl4030->master_substream)
		slv_substream = twl4030->slave_substream;
	else if (mst_substream == twl4030->slave_substream)
		slv_substream = twl4030->master_substream;
	else /* This should not happen.. */
		return;

	/* Set the constraints according to the already configured stream */
	snd_pcm_hw_constraint_minmax(slv_substream->runtime,
				SNDRV_PCM_HW_PARAM_RATE,
				twl4030->rate,
				twl4030->rate);

	snd_pcm_hw_constraint_minmax(slv_substream->runtime,
				SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
				twl4030->sample_bits,
				twl4030->sample_bits);

	snd_pcm_hw_constraint_minmax(slv_substream->runtime,
				SNDRV_PCM_HW_PARAM_CHANNELS,
				twl4030->channels,
				twl4030->channels);
}

static int twl4030_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct twl4030_priv *twl4030 = codec->private_data;

	if (twl4030->master_substream) {
		twl4030->slave_substream = substream;
		/* The DAI has one configuration for playback and capture, so
		 * if the DAI has been already configured then constrain this
		 * substream to match it. */
		if (twl4030->configured)
			twl4030_constraints(twl4030, twl4030->master_substream);
	} else {
		twl4030->master_substream = substream;
	}

	return 0;
}

static void twl4030_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct twl4030_priv *twl4030 = codec->private_data;

	if (twl4030->master_substream == substream)
		twl4030->master_substream = twl4030->slave_substream;

	twl4030->slave_substream = NULL;

	/* If all streams are closed, or the remaining stream has not yet
	 * been configured than set the DAI as not configured. */
	if (!twl4030->master_substream)
		twl4030->configured = 0;
	 else if (!twl4030->master_substream->runtime->channels)
		twl4030->configured = 0;
}

static int twl4030_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct twl4030_priv *twl4030 = codec->private_data;
	u8 mode, old_mode, format, old_format;

	if (twl4030->configured)
		/* Ignoring hw_params for already configured DAI */
		return 0;

	/* bit rate */
	old_mode = twl4030_read_reg_cache(codec,
			TWL4030_REG_CODEC_MODE) & ~TWL4030_CODECPDZ;
	mode = old_mode & ~TWL4030_APLL_RATE;

	switch (params_rate(params)) {
	case 8000:
		mode |= TWL4030_APLL_RATE_8000;
		break;
	case 11025:
		mode |= TWL4030_APLL_RATE_11025;
		break;
	case 12000:
		mode |= TWL4030_APLL_RATE_12000;
		break;
	case 16000:
		mode |= TWL4030_APLL_RATE_16000;
		break;
	case 22050:
		mode |= TWL4030_APLL_RATE_22050;
		break;
	case 24000:
		mode |= TWL4030_APLL_RATE_24000;
		break;
	case 32000:
		mode |= TWL4030_APLL_RATE_32000;
		break;
	case 44100:
		mode |= TWL4030_APLL_RATE_44100;
		break;
	case 48000:
		mode |= TWL4030_APLL_RATE_48000;
		break;
	case 96000:
		mode |= TWL4030_APLL_RATE_96000;
		break;
	default:
		printk(KERN_ERR "TWL4030 hw params: unknown rate %d\n",
			params_rate(params));
		return -EINVAL;
	}

	if (mode != old_mode) {
		/* change rate and set CODECPDZ */
		twl4030_codec_enable(codec, 0);
		twl4030_write(codec, TWL4030_REG_CODEC_MODE, mode);
		twl4030_codec_enable(codec, 1);
	}

	/* sample size */
	old_format = twl4030_read_reg_cache(codec, TWL4030_REG_AUDIO_IF);
	format = old_format;
	format &= ~TWL4030_DATA_WIDTH;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		format |= TWL4030_DATA_WIDTH_16S_16W;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		format |= TWL4030_DATA_WIDTH_32S_24W;
		break;
	default:
		printk(KERN_ERR "TWL4030 hw params: unknown format %d\n",
			params_format(params));
		return -EINVAL;
	}

	if (format != old_format) {

		/* clear CODECPDZ before changing format (codec requirement) */
		twl4030_codec_enable(codec, 0);

		/* change format */
		twl4030_write(codec, TWL4030_REG_AUDIO_IF, format);

		/* set CODECPDZ afterwards */
		twl4030_codec_enable(codec, 1);
	}

	/* Store the important parameters for the DAI configuration and set
	 * the DAI as configured */
	twl4030->configured = 1;
	twl4030->rate = params_rate(params);
	twl4030->sample_bits = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_SAMPLE_BITS)->min;
	twl4030->channels = params_channels(params);

	/* If both playback and capture streams are open, and one of them
	 * is setting the hw parameters right now (since we are here), set
	 * constraints to the other stream to match the current one. */
	if (twl4030->slave_substream)
		twl4030_constraints(twl4030, substream);

	return 0;
}

static int twl4030_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u8 infreq;

	switch (freq) {
	case 19200000:
		infreq = TWL4030_APLL_INFREQ_19200KHZ;
		break;
	case 26000000:
		infreq = TWL4030_APLL_INFREQ_26000KHZ;
		break;
	case 38400000:
		infreq = TWL4030_APLL_INFREQ_38400KHZ;
		break;
	default:
		printk(KERN_ERR "TWL4030 set sysclk: unknown rate %d\n",
			freq);
		return -EINVAL;
	}

	infreq |= TWL4030_APLL_EN;
	twl4030_write(codec, TWL4030_REG_APLL_CTL, infreq);

	return 0;
}

static int twl4030_set_dai_fmt(struct snd_soc_dai *codec_dai,
			     unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u8 old_format, format;

	/* get format */
	old_format = twl4030_read_reg_cache(codec, TWL4030_REG_AUDIO_IF);
	format = old_format;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		format &= ~(TWL4030_AIF_SLAVE_EN);
		format &= ~(TWL4030_CLK256FS_EN);
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		format |= TWL4030_AIF_SLAVE_EN;
		format |= TWL4030_CLK256FS_EN;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	format &= ~TWL4030_AIF_FORMAT;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format |= TWL4030_AIF_FORMAT_CODEC;
		break;
	default:
		return -EINVAL;
	}

	if (format != old_format) {

		/* clear CODECPDZ before changing format (codec requirement) */
		twl4030_codec_enable(codec, 0);

		/* change format */
		twl4030_write(codec, TWL4030_REG_AUDIO_IF, format);

		/* set CODECPDZ afterwards */
		twl4030_codec_enable(codec, 1);
	}

	return 0;
}

#define TWL4030_RATES	 (SNDRV_PCM_RATE_8000_48000)
#define TWL4030_FORMATS	 (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FORMAT_S24_LE)

static struct snd_soc_dai_ops twl4030_dai_ops = {
	.startup	= twl4030_startup,
	.shutdown	= twl4030_shutdown,
	.hw_params	= twl4030_hw_params,
	.set_sysclk	= twl4030_set_dai_sysclk,
	.set_fmt	= twl4030_set_dai_fmt,
};

struct snd_soc_dai twl4030_dai = {
	.name = "twl4030",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TWL4030_RATES | SNDRV_PCM_RATE_96000,
		.formats = TWL4030_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TWL4030_RATES,
		.formats = TWL4030_FORMATS,},
	.ops = &twl4030_dai_ops,
};
EXPORT_SYMBOL_GPL(twl4030_dai);

static int twl4030_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	twl4030_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int twl4030_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	twl4030_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	twl4030_set_bias_level(codec, codec->suspend_bias_level);
	return 0;
}

/*
 * initialize the driver
 * register the mixer and dsp interfaces with the kernel
 */

static int twl4030_init(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev->card->codec;
	int ret = 0;

	printk(KERN_INFO "TWL4030 Audio Codec init \n");

	codec->name = "twl4030";
	codec->owner = THIS_MODULE;
	codec->read = twl4030_read_reg_cache;
	codec->write = twl4030_write;
	codec->set_bias_level = twl4030_set_bias_level;
	codec->dai = &twl4030_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = sizeof(twl4030_reg);
	codec->reg_cache = kmemdup(twl4030_reg, sizeof(twl4030_reg),
					GFP_KERNEL);
	if (codec->reg_cache == NULL)
		return -ENOMEM;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "twl4030: failed to create pcms\n");
		goto pcm_err;
	}

	twl4030_init_chip(codec);

	/* power on device */
	twl4030_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	snd_soc_add_controls(codec, twl4030_snd_controls,
				ARRAY_SIZE(twl4030_snd_controls));
	twl4030_add_widgets(codec);

	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		printk(KERN_ERR "twl4030: failed to register card\n");
		goto card_err;
	}

	return ret;

card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	kfree(codec->reg_cache);
	return ret;
}

static struct snd_soc_device *twl4030_socdev;

static int twl4030_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	struct twl4030_priv *twl4030;

	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	twl4030 = kzalloc(sizeof(struct twl4030_priv), GFP_KERNEL);
	if (twl4030 == NULL) {
		kfree(codec);
		return -ENOMEM;
	}

	codec->private_data = twl4030;
	socdev->card->codec = codec;
	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	twl4030_socdev = socdev;
	twl4030_init(socdev);

	return 0;
}

static int twl4030_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	printk(KERN_INFO "TWL4030 Audio Codec remove\n");
	twl4030_set_bias_level(codec, SND_SOC_BIAS_OFF);
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
	kfree(codec->private_data);
	kfree(codec);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_twl4030 = {
	.probe = twl4030_probe,
	.remove = twl4030_remove,
	.suspend = twl4030_suspend,
	.resume = twl4030_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_twl4030);

static int __init twl4030_modinit(void)
{
	return snd_soc_register_dai(&twl4030_dai);
}
module_init(twl4030_modinit);

static void __exit twl4030_exit(void)
{
	snd_soc_unregister_dai(&twl4030_dai);
}
module_exit(twl4030_exit);

MODULE_DESCRIPTION("ASoC TWL4030 codec driver");
MODULE_AUTHOR("Steve Sakoman");
MODULE_LICENSE("GPL");
