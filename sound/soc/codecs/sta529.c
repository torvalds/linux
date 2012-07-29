/*
 * ASoC codec driver for spear platform
 *
 * sound/soc/codecs/sta529.c -- spear ALSA Soc codec driver
 *
 * Copyright (C) 2012 ST Microelectronics
 * Rajeev Kumar <rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

/* STA529 Register offsets */
#define	 STA529_FFXCFG0		0x00
#define	 STA529_FFXCFG1		0x01
#define	 STA529_MVOL		0x02
#define	 STA529_LVOL		0x03
#define	 STA529_RVOL		0x04
#define	 STA529_TTF0		0x05
#define	 STA529_TTF1		0x06
#define	 STA529_TTP0		0x07
#define	 STA529_TTP1		0x08
#define	 STA529_S2PCFG0		0x0A
#define	 STA529_S2PCFG1		0x0B
#define	 STA529_P2SCFG0		0x0C
#define	 STA529_P2SCFG1		0x0D
#define	 STA529_PLLCFG0		0x14
#define	 STA529_PLLCFG1		0x15
#define	 STA529_PLLCFG2		0x16
#define	 STA529_PLLCFG3		0x17
#define	 STA529_PLLPFE		0x18
#define	 STA529_PLLST		0x19
#define	 STA529_ADCCFG		0x1E /*mic_select*/
#define	 STA529_CKOCFG		0x1F
#define	 STA529_MISC		0x20
#define	 STA529_PADST0		0x21
#define	 STA529_PADST1		0x22
#define	 STA529_FFXST		0x23
#define	 STA529_PWMIN1		0x2D
#define	 STA529_PWMIN2		0x2E
#define	 STA529_POWST		0x32

#define STA529_MAX_REGISTER	0x32

#define STA529_RATES		(SNDRV_PCM_RATE_8000 | \
				SNDRV_PCM_RATE_11025 | \
				SNDRV_PCM_RATE_16000 | \
				SNDRV_PCM_RATE_22050 | \
				SNDRV_PCM_RATE_32000 | \
				SNDRV_PCM_RATE_44100 | \
				SNDRV_PCM_RATE_48000)

#define STA529_FORMAT		(SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S24_LE | \
				SNDRV_PCM_FMTBIT_S32_LE)
#define	S2PC_VALUE		0x98
#define CLOCK_OUT		0x60
#define LEFT_J_DATA_FORMAT	0x10
#define I2S_DATA_FORMAT		0x12
#define RIGHT_J_DATA_FORMAT	0x14
#define CODEC_MUTE_VAL		0x80

#define POWER_CNTLMSAK		0x40
#define POWER_STDBY		0x40
#define FFX_MASK		0x80
#define FFX_OFF			0x80
#define POWER_UP		0x00
#define FFX_CLK_ENB		0x01
#define FFX_CLK_DIS		0x00
#define FFX_CLK_MSK		0x01
#define PLAY_FREQ_RANGE_MSK	0x70
#define CAP_FREQ_RANGE_MSK	0x0C
#define PDATA_LEN_MSK		0xC0
#define BCLK_TO_FS_MSK		0x30
#define AUDIO_MUTE_MSK		0x80

static const struct reg_default sta529_reg_defaults[] = {
	{ 0,  0x35 },     /* R0   - FFX Configuration reg 0 */
	{ 1,  0xc8 },     /* R1   - FFX Configuration reg 1 */
	{ 2,  0x50 },     /* R2   - Master Volume */
	{ 3,  0x00 },     /* R3   - Left Volume */
	{ 4,  0x00 },     /* R4  -  Right Volume */
	{ 10, 0xb2 },     /* R10  - S2P Config Reg 0 */
	{ 11, 0x41 },     /* R11  - S2P Config Reg 1 */
	{ 12, 0x92 },     /* R12  - P2S Config Reg 0 */
	{ 13, 0x41 },     /* R13  - P2S Config Reg 1 */
	{ 30, 0xd2 },     /* R30  - ADC Config Reg */
	{ 31, 0x40 },     /* R31  - clock Out Reg */
	{ 32, 0x21 },     /* R32  - Misc Register */
};

struct sta529 {
	struct regmap *regmap;
};

static bool sta529_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {

	case STA529_FFXCFG0:
	case STA529_FFXCFG1:
	case STA529_MVOL:
	case STA529_LVOL:
	case STA529_RVOL:
	case STA529_S2PCFG0:
	case STA529_S2PCFG1:
	case STA529_P2SCFG0:
	case STA529_P2SCFG1:
	case STA529_ADCCFG:
	case STA529_CKOCFG:
	case STA529_MISC:
		return true;
	default:
		return false;
	}
}


static const char *pwm_mode_text[] = { "Binary", "Headphone", "Ternary",
	"Phase-shift"};

static const DECLARE_TLV_DB_SCALE(out_gain_tlv, -9150, 50, 0);
static const DECLARE_TLV_DB_SCALE(master_vol_tlv, -12750, 50, 0);
static const SOC_ENUM_SINGLE_DECL(pwm_src, STA529_FFXCFG1, 4, pwm_mode_text);

static const struct snd_kcontrol_new sta529_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Digital Playback Volume", STA529_LVOL, STA529_RVOL, 0,
			127, 0, out_gain_tlv),
	SOC_SINGLE_TLV("Master Playback Volume", STA529_MVOL, 0, 127, 1,
			master_vol_tlv),
	SOC_ENUM("PWM Select", pwm_src),
};

static int sta529_set_bias_level(struct snd_soc_codec *codec, enum
		snd_soc_bias_level level)
{
	struct sta529 *sta529 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		snd_soc_update_bits(codec, STA529_FFXCFG0, POWER_CNTLMSAK,
				POWER_UP);
		snd_soc_update_bits(codec, STA529_MISC,	FFX_CLK_MSK,
				FFX_CLK_ENB);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF)
			regcache_sync(sta529->regmap);
		snd_soc_update_bits(codec, STA529_FFXCFG0,
					POWER_CNTLMSAK, POWER_STDBY);
		/* Making FFX output to zero */
		snd_soc_update_bits(codec, STA529_FFXCFG0, FFX_MASK,
				FFX_OFF);
		snd_soc_update_bits(codec, STA529_MISC, FFX_CLK_MSK,
				FFX_CLK_DIS);
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}

	/*
	 * store the label for powers down audio subsystem for suspend.This is
	 * used by soc core layer
	 */
	codec->dapm.bias_level = level;

	return 0;

}

static int sta529_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	int pdata, play_freq_val, record_freq_val;
	int bclk_to_fs_ratio;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		pdata = 1;
		bclk_to_fs_ratio = 0;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		pdata = 2;
		bclk_to_fs_ratio = 1;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		pdata = 3;
		bclk_to_fs_ratio = 2;
		break;
	default:
		dev_err(codec->dev, "Unsupported format\n");
		return -EINVAL;
	}

	switch (params_rate(params)) {
	case 8000:
	case 11025:
		play_freq_val = 0;
		record_freq_val = 2;
		break;
	case 16000:
	case 22050:
		play_freq_val = 1;
		record_freq_val = 0;
		break;

	case 32000:
	case 44100:
	case 48000:
		play_freq_val = 2;
		record_freq_val = 0;
		break;
	default:
		dev_err(codec->dev, "Unsupported rate\n");
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		snd_soc_update_bits(codec, STA529_S2PCFG1, PDATA_LEN_MSK,
				pdata << 6);
		snd_soc_update_bits(codec, STA529_S2PCFG1, BCLK_TO_FS_MSK,
				bclk_to_fs_ratio << 4);
		snd_soc_update_bits(codec, STA529_MISC, PLAY_FREQ_RANGE_MSK,
				play_freq_val << 4);
	} else {
		snd_soc_update_bits(codec, STA529_P2SCFG1, PDATA_LEN_MSK,
				pdata << 6);
		snd_soc_update_bits(codec, STA529_P2SCFG1, BCLK_TO_FS_MSK,
				bclk_to_fs_ratio << 4);
		snd_soc_update_bits(codec, STA529_MISC, CAP_FREQ_RANGE_MSK,
				record_freq_val << 2);
	}

	return 0;
}

static int sta529_mute(struct snd_soc_dai *dai, int mute)
{
	u8 val = 0;

	if (mute)
		val |= CODEC_MUTE_VAL;

	snd_soc_update_bits(dai->codec, STA529_FFXCFG0, AUDIO_MUTE_MSK, val);

	return 0;
}

static int sta529_set_dai_fmt(struct snd_soc_dai *codec_dai, u32 fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u8 mode = 0;

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		mode = LEFT_J_DATA_FORMAT;
		break;
	case SND_SOC_DAIFMT_I2S:
		mode = I2S_DATA_FORMAT;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		mode = RIGHT_J_DATA_FORMAT;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, STA529_S2PCFG0, 0x0D, mode);

	return 0;
}

static const struct snd_soc_dai_ops sta529_dai_ops = {
	.hw_params	=	sta529_hw_params,
	.set_fmt	=	sta529_set_dai_fmt,
	.digital_mute	=	sta529_mute,
};

static struct snd_soc_dai_driver sta529_dai = {
	.name = "sta529-audio",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = STA529_RATES,
		.formats = STA529_FORMAT,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = STA529_RATES,
		.formats = STA529_FORMAT,
	},
	.ops	= &sta529_dai_ops,
};

static int sta529_probe(struct snd_soc_codec *codec)
{
	struct sta529 *sta529 = snd_soc_codec_get_drvdata(codec);
	int ret;

	codec->control_data = sta529->regmap;
	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_REGMAP);

	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}
	sta529_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

/* power down chip */
static int sta529_remove(struct snd_soc_codec *codec)
{
	sta529_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int sta529_suspend(struct snd_soc_codec *codec)
{
	sta529_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int sta529_resume(struct snd_soc_codec *codec)
{
	sta529_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

struct snd_soc_codec_driver sta529_codec_driver = {
	.probe = sta529_probe,
	.remove = sta529_remove,
	.set_bias_level = sta529_set_bias_level,
	.suspend = sta529_suspend,
	.resume = sta529_resume,
	.controls = sta529_snd_controls,
	.num_controls = ARRAY_SIZE(sta529_snd_controls),
};

static const struct regmap_config sta529_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = STA529_MAX_REGISTER,
	.readable_reg = sta529_readable,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = sta529_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(sta529_reg_defaults),
};

static __devinit int sta529_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct sta529 *sta529;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EINVAL;

	sta529 = devm_kzalloc(&i2c->dev, sizeof(struct sta529), GFP_KERNEL);
	if (sta529 == NULL) {
		dev_err(&i2c->dev, "Can not allocate memory\n");
		return -ENOMEM;
	}

	sta529->regmap = devm_regmap_init_i2c(i2c, &sta529_regmap);
	if (IS_ERR(sta529->regmap)) {
		ret = PTR_ERR(sta529->regmap);
		dev_err(&i2c->dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, sta529);

	ret = snd_soc_register_codec(&i2c->dev,
			&sta529_codec_driver, &sta529_dai, 1);
	if (ret != 0)
		dev_err(&i2c->dev, "Failed to register CODEC: %d\n", ret);

	return ret;
}

static int __devexit sta529_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);

	return 0;
}

static const struct i2c_device_id sta529_i2c_id[] = {
	{ "sta529", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sta529_i2c_id);

static struct i2c_driver sta529_i2c_driver = {
	.driver = {
		.name = "sta529",
		.owner = THIS_MODULE,
	},
	.probe		= sta529_i2c_probe,
	.remove		= __devexit_p(sta529_i2c_remove),
	.id_table	= sta529_i2c_id,
};

module_i2c_driver(sta529_i2c_driver);

MODULE_DESCRIPTION("ASoC STA529 codec driver");
MODULE_AUTHOR("Rajeev Kumar <rajeev-dlh.kumar@st.com>");
MODULE_LICENSE("GPL");
