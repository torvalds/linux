#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>

#ifdef CONFIG_DEBUG_FS
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/string.h>
#endif

#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/jack.h>

#include "rt5512.h"

#define RT5512_SRATE_MASK 0x7
#define RT5512_AUDBIT_MASK 0x3
#define RT5512_MCLKSELBIT_MASK 0xf
#define RT5512_CLKBASEBIT_MASK 0x2
#define RT5512_AUDFMT_MASK 0xc
#define RT5512_PDBIT_MASK 0x40
#define RT5512_RSTBIT_MASK 0x80

#define RT5512_RATES SNDRV_PCM_RATE_8000_96000
#define RT5512_FORMATS (SNDRV_PCM_FMTBIT_S16_LE|SNDRV_PCM_FMTBIT_S20_3LE|SNDRV_PCM_FMTBIT_S24_LE)

#define RT5512_MAX_REG 0xEC
#define RT5512_VIRTUAL_REG	0xEC

struct rt5512_codec_reg 
{
	unsigned char reg_size;
	unsigned int reg_cache;  // support maximum 4 byte data
	unsigned char sync_needed;
};

static struct rt5512_codec_reg rt5512_reg_priv[RT5512_MAX_REG+1] =
{
	[0x00] = { 1, 0, 0},
	[0x01] = { 1, 0, 0},
	[0x02] = { 1, 0, 0},
	[0x03] = { 1, 0, 0},
	[0x04] = { 1, 0, 0},
	[0x05] = { 1, 0, 0},
	[0x06] = { 1, 0, 0},
	[0x07] = { 1, 0, 0},
	[0x08] = { 4, 0, 0},
	[0x09] = { 4, 0, 0},
	[0x0A] = { 4, 0, 0},
	[0x0B] = { 4, 0, 0},
	[0x0C] = { 4, 0, 0},
	[0x0D] = { 4, 0, 0},
	[0x0E] = { 4, 0, 0},
	[0x0F] = { 4, 0, 0},
	[0x10] = { 1, 0, 0},
	[0x11] = { 1, 0, 0},
	[0x12] = { 1, 0, 0},
	[0x13] = { 1, 0, 0},
	[0x14] = { 1, 0, 0},
	[0x15] = { 1, 0, 0},
	[0x16] = { 1, 0, 0},
	[0x17] = { 1, 0, 0},
	[0x1A] = { 1, 0, 0},
	[0x1B] = { 1, 0, 0},
	[0x1C] = { 1, 0, 0},
	[0x1D] = { 4, 0, 0},
	[0x1F] = { 1, 0, 0},
	[0x20] = { 20, 0, 0},
	[0x21] = { 20, 0, 0},
	[0x22] = { 20, 0, 0},
	[0x23] = { 20, 0, 0},
	[0x24] = { 20, 0, 0},
	[0x25] = { 20, 0, 0},
	[0x26] = { 20, 0, 0},
	[0x27] = { 20, 0, 0},
	[0x28] = { 20, 0, 0},
	[0x29] = { 20, 0, 0},
	[0x2A] = { 20, 0, 0},
	[0x2B] = { 20, 0, 0},
	[0x2C] = { 20, 0, 0},
	[0x30] = { 1, 0, 0},
	[0x31] = { 1, 0, 0},
	[0x38] = { 8, 0, 0},
	[0x39] = { 8, 0, 0},
	[0x3A] = { 8, 0, 0},
	[0x3B] = { 8, 0, 0},
	[0x3C] = { 8, 0, 0},
	[0x3D] = { 8, 0, 0},
	[0x3E] = { 8, 0, 0},
	[0x3F] = { 8, 0, 0},
	[0x40] = { 16, 0, 0},
	[0x41] = { 16, 0, 0},
	[0x42] = { 16, 0, 0},
	[0x43] = { 16, 0, 0},
	[0x44] = { 16, 0, 0},
	[0x45] = { 16, 0, 0},
	[0x50] = { 1, 0, 0},
	[0x51] = { 1, 0, 0},
	[0x52] = { 1, 0, 0},
	[0x55] = { 4, 0, 0},
	[0x56] = { 1, 0, 0},
	[0x57] = { 1, 0, 0},
	[0x58] = { 1, 0, 0},
	[0x59] = { 1, 0, 0},
	[0x5B] = { 1, 0, 0},
	[0x5C] = { 1, 0, 0},
	[0x5D] = { 1, 0, 0},
	[0x75] = { 1, 0, 0},
	[0x78] = { 1, 0, 0},
	[0x80] = { 1, 0, 0},
	[0x81] = { 1, 0, 0},
	[0x82] = { 1, 0, 0},
	[0x83] = { 1, 0, 0},
	[0x84] = { 1, 0, 0},
	[0x85] = { 1, 0, 0},
	[0x86] = { 1, 0, 0},
	[0x87] = { 1, 0, 0},
	[0x88] = { 1, 0, 0},
	[0x89] = { 1, 0, 0},
	[0x8A] = { 1, 0, 0},
	[0x8B] = { 1, 0, 0},
	[0x8C] = { 1, 0, 0},
	[0x8D] = { 1, 0, 0},
	[0x8E] = { 1, 0, 0},
	[0x8F] = { 1, 0, 0},
	[0x90] = { 1, 0, 0},
	[0x91] = { 1, 0, 0},
	[0x92] = { 1, 0, 0},
	[0x93] = { 1, 0, 0},
	[0x94] = { 1, 0, 0},
	[0x95] = { 1, 0, 0},
	[0x96] = { 1, 0, 0},
	[0x97] = { 1, 0, 0},
	[0x98] = { 1, 0, 0},
	[0x99] = { 1, 0, 0},
	[0x9A] = { 1, 0, 0},
	[0x9B] = { 1, 0, 0},
	[0x9C] = { 1, 0, 0},
	[0x9D] = { 1, 0, 0},
	[0x9E] = { 1, 0, 0},
	[0x9F] = { 1, 0, 0},
	[0xA0] = { 1, 0, 0},
	[0xA1] = { 1, 0, 0},
	[0xA2] = { 1, 0, 0},
	[0xA3] = { 1, 0, 0},
	[0xA4] = { 1, 0, 0},
	[0xA5] = { 1, 0, 0},
	[0xA6] = { 1, 0, 0},
	[0xA7] = { 1, 0, 0},
	[0xA8] = { 1, 0, 0},
	[0xCA] = { 1, 0, 0},
	[0xCC] = { 1, 0, 0},
	[0xCD] = { 1, 0, 0},
	[0xCE] = { 1, 0, 0},
    [0xE0] = { 1, 0, 0},   
	[0xE8] = { 1, 0, 0},
	[0xEB] = { 1, 0, 0},
	[0xEC] = { 4, 0, 0},
};

#if FOR_MID

struct rt5512_init_reg {
    u8 reg;
    u32 regval;
};

static struct rt5512_init_reg init_list[] = {
    /*playback*/
    {0x10 , 0x30},
    {0x11 , 0x30},
    {0x82 , 0x30},
    {0x84 , 0x03},
    {0x8c , 0x09},
    {0xec , 0x27c},
    /*record*/
    {0x95 , 0x82},
    {0x85 , 0x30},  
};
#define RT5512_INIT_REG_LEN ARRAY_SIZE(init_list)

static int rt5512_reg_init(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < RT5512_INIT_REG_LEN; i++)
		snd_soc_write(codec, init_list[i].reg, init_list[i].regval);

	return 0;
}
#endif

static int rt5512_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned short  srate_reg;
	unsigned int new_srate_regval, new_datfmt_regval;

	// set sampling rate
	RT_DBG("codec hw_param\n");
	RT_DBG("codec hw_param dai id %d\n", dai->id);
	switch (dai->id)
	{
		case 0: // AIF1
			srate_reg = 0x03;
			break;
		case 1: // AIF2
			srate_reg = 0x04;
			break;
		default:
			dev_err(codec->dev, "this codec dai id is not supported\n");
			return -EINVAL;
	}

	RT_DBG("codec hw_param rate%d\n", params_rate(params));
	switch (params_rate(params))
	{
		case 8000:
			new_srate_regval = 0x0;
			break;
		case 11025:
		case 12000:
			new_srate_regval = 0x1;
			break;
		case 16000:
			new_srate_regval = 0x2;
			break;
		case 22050:
		case 24000:
			new_srate_regval = 0x3;
			break;
		case 32000:
			new_srate_regval = 0x4;
			break;
		case 44100:
		case 48000:
			new_srate_regval = 0x5;
			break;
		case 88200:
		case 96000:
			new_srate_regval = 0x6;
			break;
		default:
			dev_err(codec->dev, "not supported sampling rate\n");
			return -EINVAL;
	}
	snd_soc_update_bits(codec, srate_reg, RT5512_SRATE_MASK, new_srate_regval);

	// set data format
	RT_DBG("codec hw_param format%d\n", params_format(params));
	switch (params_format(params))
	{
		case SNDRV_PCM_FORMAT_S16_LE:
			new_datfmt_regval = 0x3;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			new_datfmt_regval = 0x1;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			new_datfmt_regval = 0x0;
			break;
		default:
			dev_err(codec->dev, "not supported data bit format\n");
			return -EINVAL;
	}
	snd_soc_update_bits(codec, 0x02, RT5512_AUDBIT_MASK, new_datfmt_regval);

	RT_DBG("stream direction %d\n", substream->stream);
	if (substream->stream == 0)  //Playback
		snd_soc_write(codec, 0x56, 0xA4);
	else if (substream->stream == 1) //Record
	{
		snd_soc_update_bits(codec, 0x9F, 0xC0, 0x40);
		snd_soc_update_bits(codec, 0x81, 0x40, 0x00);
		snd_soc_write(codec, 0x9E, 0x00);
	}

	RT_DBG("special register configuration\n");
    snd_soc_write(codec, 0xE0, 0x2F);
	snd_soc_write(codec, 0xEB, 0xB4);
	snd_soc_write(codec, 0xE8, 0x53);

	return 0;
}

static int rt5512_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int new_mclk_sel, new_clk_basesel;

	RT_DBG("codec dai_sysclk\n");
	switch (dai->id)
	{
		case 0: // AIF1
			new_mclk_sel = 0x1;
			break;
		case 1: // AIF2
			new_mclk_sel = 0x1;
			break;
		default:
			dev_err(codec->dev, "no this codec dai id\n");
			return -EINVAL;
	}
	snd_soc_update_bits(codec, 0x01, RT5512_MCLKSELBIT_MASK, new_mclk_sel);

	if (freq == 11289600)   //22.579200 MHz
		new_clk_basesel = RT5512_CLKBASEBIT_MASK;
	else                    //24.576000  MHz
		new_clk_basesel = 0;

	snd_soc_update_bits(codec, 0x1A, RT5512_CLKBASEBIT_MASK, new_clk_basesel);

	snd_soc_dapm_sync(&codec->dapm);
	return 0;
}

static int rt5512_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int mute_mask;

	RT_DBG("codec digital_mute id = %d\n", dai->id);
	switch (dai->id)
	{
		case 0:  // AIF1
			mute_mask = 0x03;
			break;
		case 1:  // AIF2
			mute_mask = 0x0C;
			break;
		default:
			dev_err(codec->dev, "no this codec dai id\n");
			return -EINVAL;
	}
	snd_soc_update_bits(codec, 0x07, mute_mask, (mute?mute_mask:0));
    if (!mute)
        msleep(50);
	return 0;
}

static int rt5512_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int new_daifmt_regval;

	RT_DBG("codec dai_fmt\n");
	if (fmt & SND_SOC_DAIFMT_I2S)
		new_daifmt_regval = 0x00;
	else if (fmt & SND_SOC_DAIFMT_RIGHT_J)
		new_daifmt_regval = 0x08;
	else if (fmt & SND_SOC_DAIFMT_LEFT_J)
		new_daifmt_regval = 0x04;
	else
		new_daifmt_regval = 0x00;

	snd_soc_update_bits(codec, 0x02, RT5512_AUDFMT_MASK, new_daifmt_regval);
	return 0;
}

static int rt5512_set_clkdiv(struct snd_soc_dai *codec_dai, int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;

	RT_DBG("codec clk_div(%d) value %d\n", div_id, div);
	switch (div_id)
	{
		case RT5512_CLK_DIV_ID:
			snd_soc_write(codec, 0x55, (div&0x1ffff) << 16);
			break;
		default:
			dev_err(codec->dev, "Invalid clock divider");
			return -EINVAL;
	}

	return 0;
}

static struct snd_soc_dai_ops rt5512_dai_ops = 
{
	.hw_params	= rt5512_hw_params,
	.digital_mute	= rt5512_digital_mute,
	.set_sysclk	= rt5512_set_dai_sysclk,
	.set_fmt	= rt5512_set_dai_fmt,
	.set_clkdiv	= rt5512_set_clkdiv,
};

static struct snd_soc_dai_driver rt5512_dai[] = 
{
	// AIF1
	{
	.name = "RT5512-aif1",
	.playback = 
	{
		.stream_name = "AIF1 Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = RT5512_RATES,
		.formats = RT5512_FORMATS,
	},
	.capture = 
	{
		.stream_name = "AIF1 Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = RT5512_RATES,
		.formats = RT5512_FORMATS,
	},
	.ops = &rt5512_dai_ops,
	.symmetric_rates = 1,
	},
	#if 0
	// AIF2
	{
	.name = "RT5512-aif2",
	.playback = 
	{
		.stream_name = "AIF2 Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = RT5512_RATES,
		.formats = RT5512_FORMATS,
	},
	.capture = 
	{
		.stream_name = "AIF2 Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = RT5512_RATES,
		.formats = RT5512_FORMATS,
	},
	.ops = &rt5512_dai_ops,
	.symmetric_rates = 1,
	},
	#endif /* #if 0 */
};

static const DECLARE_TLV_DB_SCALE(mic_pga_tlv, -1650, 150, 0);
static const DECLARE_TLV_DB_SCALE(mic_lrpga_tlv, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(aux_pga_tlv, -500, 100, 0);   // range from -5dB to 26dB
static const DECLARE_TLV_DB_SCALE(aux_lrpga_tlv, -300, 300, 0);
static const DECLARE_TLV_DB_SCALE(lrpga2lrspk_tlv, -600, 300, 0);
static const DECLARE_TLV_DB_SCALE(lrpga2lrhp_tlv, -6000, 300, 0);
static const DECLARE_TLV_DB_SCALE(lrspkpga_tlv, -4300, 100, 0);
static const DECLARE_TLV_DB_SCALE(lrhppga_tlv, -3100, 100, 0);
static const DECLARE_TLV_DB_SCALE(recvoutdrv_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(spkoutdrv_tlv, 600, 300, 0);
static const DECLARE_TLV_DB_SCALE(hpoutdrv_tlv, -1200, 100, 0);
static const DECLARE_TLV_DB_SCALE(digital_tlv, -10375, 25, 0);
static const char *mic_mode_sel[] = {"Single Ended", "Differential"};
static const char *mic_inv_mode_sel[] = {"Non-inverting mode", "Inverting mode"};
static const char *analog_bypass_sel[] = {"Aux", "Mic1", "Mic2", "Mic3"};
static const char *digital_output_percent[] = {"zero", "quarter", "half", "one"};
static const char *out_path[] = { "Off", "Recv", "LR_SPK", "LR_HP"};
static const char *in_path[] = {"Off", "Mic1", "Mic2", "Mic3", "Aux"};
static const char *hpf_freqcut_option[] = {"4Hz", "379Hz", "770Hz", "1594Hz"};
static const struct soc_enum rt5512_enum[] =
{
	SOC_ENUM_SINGLE(0x92, 7, 2, mic_mode_sel),
	SOC_ENUM_SINGLE(0x92, 6, 2, mic_inv_mode_sel),
	SOC_ENUM_SINGLE(0x98, 0, 4, analog_bypass_sel),
	SOC_ENUM_SINGLE(0x10, 6, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x10, 4, 4, digital_output_percent), //4
	SOC_ENUM_SINGLE(0x10, 2, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x10, 0, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x11, 6, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x11, 4, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x11, 2, 4, digital_output_percent), 
	SOC_ENUM_SINGLE(0x11, 0, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x12, 6, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x12, 4, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x12, 2, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x12, 0, 4, digital_output_percent), 
	SOC_ENUM_SINGLE(0x13, 6, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x13, 4, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x13, 2, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x13, 0, 4, digital_output_percent),
	SOC_ENUM_SINGLE(0x93, 7, 2, mic_mode_sel),           
	SOC_ENUM_SINGLE(0x93, 6, 2, mic_inv_mode_sel),
	SOC_ENUM_SINGLE(0x94, 7, 2, mic_mode_sel),
	SOC_ENUM_SINGLE(0x94, 6, 2, mic_inv_mode_sel),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(out_path), out_path),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(in_path), in_path),   
	SOC_ENUM_SINGLE(0x17, 0, 4, hpf_freqcut_option),
	SOC_ENUM_SINGLE(0x17, 2, 4, hpf_freqcut_option),
	SOC_ENUM_SINGLE(0x17, 4, 4, hpf_freqcut_option),
};

#if FOR_MID
static const char *rt5512_playback_path_mode[] = {"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT", "SPK_HP", //0-6
													"RING_SPK", "RING_HP", "RING_HP_NO_MIC", "RING_SPK_HP"};//7-10

static const SOC_ENUM_SINGLE_DECL(rt5512_path_type, 0, 0, rt5512_playback_path_mode);

static int rt5512_playback_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5512_codec_chip *chip = snd_soc_codec_get_drvdata(codec);

	RT_DBG("\n");
    RT_DBG("%s:playback_path=%ld\n",__func__,ucontrol->value.integer.value[0]);
    
	ucontrol->value.integer.value[0] = chip->curr_outpath;
	return 0;
}


static int rt5512_playback_path_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5512_codec_chip *chip = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	RT_DBG("\n");
	if (ucontrol->value.integer.value[0] != chip->curr_outpath)
	{
	    mutex_lock(&codec->mutex);
		switch (ucontrol->value.integer.value[0])
		{
			case OFF:
                RT_DBG(">>>>>>>>>>>>>>>>%s OFF",__FUNCTION__);
				snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
                snd_soc_dapm_disable_pin(dapm, "Ext Spk");
                snd_soc_dapm_sync(dapm);
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;

            case RCV:
		        break;
                
			case SPK_PATH:
	        case RING_SPK:
                RT_DBG(">>>>>>>>>>>>>>>>%s spk",__FUNCTION__);                
                snd_soc_dapm_enable_pin(dapm, "Ext Spk");
                snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
                snd_soc_dapm_sync(dapm);
                RT_DBG("Ext Spk status=%d\n", snd_soc_dapm_get_pin_status(dapm, "Ext Spk"));
                RT_DBG("Headphone Jack status=%d\n", snd_soc_dapm_get_pin_status(dapm, "Headphone Jack"));
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;
                
			case HP_PATH:
	        case HP_NO_MIC:
	        case RING_HP:
	        case RING_HP_NO_MIC:
                RT_DBG(">>>>>>>>>>>>>>>>%s hp",__FUNCTION__);
				snd_soc_dapm_disable_pin(dapm, "Ext Spk");
                snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
                snd_soc_dapm_sync(dapm);
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;
                
            case BT:
		        break;

            case SPK_HP:
	        case RING_SPK_HP:
                RT_DBG(">>>>>>>>>>>>>>>>%s spk+hp",__FUNCTION__);
                snd_soc_dapm_enable_pin(dapm, "Ext Spk");
				snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
                snd_soc_dapm_sync(dapm);
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;
                
			default:
				dev_err(codec->dev, "Not valid path\n");
                mutex_unlock(&codec->mutex);
				ret = -EINVAL;
				break;
		}
        mutex_unlock(&codec->mutex);
	}

	return ret;
}

static const char *rt5512_capture_path_mode[] = {"MIC OFF", "Main Mic", "Hands Free Mic", "BT Sco Mic"};//

static const SOC_ENUM_SINGLE_DECL(rt5512_capture_type, 0, 0, rt5512_capture_path_mode);

static int rt5512_capture_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5512_codec_chip *chip = snd_soc_codec_get_drvdata(codec);

	RT_DBG("\n");
    RT_DBG("%s:capture_path=%ld\n",__func__,ucontrol->value.integer.value[0]);
    
	ucontrol->value.integer.value[0] = chip->curr_outpath;
	return 0;
}


static int rt5512_capture_path_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5512_codec_chip *chip = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	RT_DBG("\n");
	if (ucontrol->value.integer.value[0] != chip->curr_outpath)
	{
	    mutex_lock(&codec->mutex);
		switch (ucontrol->value.integer.value[0])
		{
			case MIC_OFF:
                RT_DBG(">>>>>>>>>>>>>>>>%s MIC_OFF",__FUNCTION__);
				snd_soc_dapm_disable_pin(dapm, "Main Mic");
                snd_soc_dapm_disable_pin(dapm, "LineIn");
                snd_soc_dapm_sync(dapm);
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;
              
			case Main_Mic:
                RT_DBG(">>>>>>>>>>>>>>>>%s Main_Mic",__FUNCTION__); 
                /*Mic2 record gain*/
                snd_soc_update_bits(codec, 0x93, 0x1f, 0x15);
                snd_soc_update_bits(codec, 0x97, 0x0c, 0x08);
                snd_soc_dapm_enable_pin(dapm, "Main Mic");
                snd_soc_dapm_disable_pin(dapm, "LineIn");
                snd_soc_dapm_sync(dapm);
                RT_DBG("Main Mic status=%d\n", snd_soc_dapm_get_pin_status(dapm, "Main Mic"));
                RT_DBG("LineIn status=%d\n", snd_soc_dapm_get_pin_status(dapm, "LineIn"));
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;
                
			case Hands_Free_Mic:
                RT_DBG(">>>>>>>>>>>>>>>>%s Hands_Free_Mic",__FUNCTION__);
                /*Aux record gain*/
                snd_soc_update_bits(codec, 0x91, 0x1f, 0x15);
                snd_soc_update_bits(codec, 0x96, 0xc0, 0x80);
				snd_soc_dapm_disable_pin(dapm, "Main Mic");
                snd_soc_dapm_enable_pin(dapm, "LineIn");
                snd_soc_dapm_sync(dapm);
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;
                
            case BT_Sco_Mic:
		        break;
                
			default:
				dev_err(codec->dev, "Not valid path\n");
                mutex_unlock(&codec->mutex);
				ret = -EINVAL;
				break;
		}
        mutex_unlock(&codec->mutex);
	}

	return ret;
}

#else
static int rt5512_inpath_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5512_codec_chip *chip = snd_soc_codec_get_drvdata(codec);

	RT_DBG("\n");
	ucontrol->value.integer.value[0] = chip->curr_inpath;
	return 0;
}

static int rt5512_inpath_set(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5512_codec_chip *chip = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	RT_DBG("\n");
	if (ucontrol->value.integer.value[0] != chip->curr_inpath)
	{
		switch (ucontrol->value.integer.value[0])
		{
			case 0:
				snd_soc_dapm_disable_pin(dapm, "Mic1");
				snd_soc_dapm_disable_pin(dapm, "Mic2");
				snd_soc_dapm_disable_pin(dapm, "Mic3");
				snd_soc_dapm_disable_pin(dapm, "Aux");
				chip->curr_inpath = ucontrol->value.integer.value[0];
				break;
			case 1:
				snd_soc_dapm_enable_pin(dapm, "Mic1");
				snd_soc_dapm_disable_pin(dapm, "Mic2");
				snd_soc_dapm_disable_pin(dapm, "Mic3");
				snd_soc_dapm_disable_pin(dapm, "Aux");
				chip->curr_inpath = ucontrol->value.integer.value[0];
				break;
			case 2:
				snd_soc_dapm_disable_pin(dapm, "Mic1");
				snd_soc_dapm_enable_pin(dapm, "Mic2");
				snd_soc_dapm_disable_pin(dapm, "Mic3");
				snd_soc_dapm_enable_pin(dapm, "Aux");
				chip->curr_inpath = ucontrol->value.integer.value[0];
				break;
			case 3:
				snd_soc_dapm_disable_pin(dapm, "Mic1");
				snd_soc_dapm_disable_pin(dapm, "Mic2");
				snd_soc_dapm_enable_pin(dapm, "Mic3");
				snd_soc_dapm_disable_pin(dapm, "Aux");
				chip->curr_inpath = ucontrol->value.integer.value[0];
				break;
			case 4:
				snd_soc_dapm_disable_pin(dapm, "Mic1");
				snd_soc_dapm_disable_pin(dapm, "Mic2");
				snd_soc_dapm_disable_pin(dapm, "Mic3");
				snd_soc_dapm_enable_pin(dapm, "Aux");
				chip->curr_inpath = ucontrol->value.integer.value[0];
				break;
			default:
				dev_err(codec->dev, "Not valid path\n");
				ret = -EINVAL;
				break;
		}
	}

	return ret;
}

static int rt5512_outpath_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5512_codec_chip *chip = snd_soc_codec_get_drvdata(codec);

	RT_DBG("\n");
	ucontrol->value.integer.value[0] = chip->curr_outpath;
	return 0;
}

static int rt5512_outpath_set(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5512_codec_chip *chip = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	RT_DBG("\n");
	if (ucontrol->value.integer.value[0] != chip->curr_outpath)
	{
		switch (ucontrol->value.integer.value[0])
		{
			case 0:
				snd_soc_dapm_disable_pin(dapm, "Receiver");
				snd_soc_dapm_disable_pin(dapm, "LSpeaker");
				snd_soc_dapm_disable_pin(dapm, "RSpeaker");
				snd_soc_dapm_disable_pin(dapm, "LHeadphone");
				snd_soc_dapm_disable_pin(dapm, "RHeadphone");
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;
			case 1:
				snd_soc_dapm_enable_pin(dapm, "Receiver");
				snd_soc_dapm_disable_pin(dapm, "LSpeaker");
				snd_soc_dapm_disable_pin(dapm, "RSpeaker");
				snd_soc_dapm_disable_pin(dapm, "LHeadphone");
				snd_soc_dapm_disable_pin(dapm, "RHeadphone");
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;
			case 2:
				snd_soc_dapm_disable_pin(dapm, "Receiver");
				snd_soc_dapm_enable_pin(dapm, "LSpeaker");
				snd_soc_dapm_enable_pin(dapm, "RSpeaker");
				snd_soc_dapm_disable_pin(dapm, "LHeadphone");
				snd_soc_dapm_disable_pin(dapm, "RHeadphone");
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;
			case 3:
				snd_soc_dapm_disable_pin(dapm, "Receiver");
				snd_soc_dapm_disable_pin(dapm, "LSpeaker");
				snd_soc_dapm_disable_pin(dapm, "RSpeaker");
				snd_soc_dapm_enable_pin(dapm, "LHeadphone");
				snd_soc_dapm_enable_pin(dapm, "RHeadphone");
				chip->curr_outpath = ucontrol->value.integer.value[0];
				break;
			default:
				dev_err(codec->dev, "Not valid path\n");
				ret = -EINVAL;
				break;
		}
	}

	return ret;
}

#endif

static const struct snd_kcontrol_new rt5512_snd_controls[] =
{
	//MicMode parameter
	SOC_ENUM("Mic1 ModeSel", rt5512_enum[0]),
	SOC_ENUM("Mic1 InvModeSel", rt5512_enum[1]),
	SOC_ENUM("Mic2 ModeSel", rt5512_enum[19]),
	SOC_ENUM("Mic2 InvModeSel", rt5512_enum[20]),
	SOC_ENUM("Mic3 ModeSel", rt5512_enum[21]),
	SOC_ENUM("Mic3 InvModeSel", rt5512_enum[22]),
	SOC_SINGLE_TLV("Aux PGA Volume",   0x91, 0, 31, 0, aux_pga_tlv),
	SOC_SINGLE_TLV("Mic1 PGA Volume",  0x92, 0, 31, 0, mic_pga_tlv),
	SOC_SINGLE_TLV("Mic2 PGA Volume",  0x93, 0, 31, 0, mic_pga_tlv),
	SOC_SINGLE_TLV("Mic3 PGA Volume",  0x94, 0, 31, 0, mic_pga_tlv),
	SOC_SINGLE_TLV("AuxL LPGA Volume", 0x96, 6,  3, 0, aux_lrpga_tlv),
	SOC_SINGLE_TLV("AuxR RPGA Volume", 0x97, 6,  3, 0, aux_lrpga_tlv),
	SOC_SINGLE_TLV("Mic1 LPGA Volume", 0x96, 4,  3, 0, mic_lrpga_tlv),
	SOC_SINGLE_TLV("Mic1 RPGA Volume", 0x97, 4,  3, 0, mic_lrpga_tlv),
	SOC_SINGLE_TLV("Mic2 LPGA Volume", 0x96, 2,  3, 0, mic_lrpga_tlv),
	SOC_SINGLE_TLV("Mic2 RPGA Volume", 0x97, 2,  3, 0, mic_lrpga_tlv),
	SOC_SINGLE_TLV("Mic3 LPGA Volume", 0x96, 0,  3, 0, mic_lrpga_tlv),
	SOC_SINGLE_TLV("Mic3 RPGA Volume", 0x97, 0,  3, 0, mic_lrpga_tlv),
	SOC_SINGLE_TLV("LPGA2LSPK PGA Volume",  0x8d, 6,  3, 0, lrpga2lrspk_tlv),
	SOC_SINGLE_TLV("RPGA2LSPK PGA Volume",  0x8d, 4,  3, 0, lrpga2lrspk_tlv),
	SOC_SINGLE_TLV("LPGA2RSPK PGA Volume",  0x8d, 2,  3, 0, lrpga2lrspk_tlv),
	SOC_SINGLE_TLV("RPGA2RSPK PGA Volume",  0x8d, 0,  3, 0, lrpga2lrspk_tlv),
	SOC_SINGLE_TLV("LPGA2LHP PGA Volume",   0x87, 6,  3, 0, lrpga2lrhp_tlv),
	SOC_SINGLE_TLV("RPGA2LHP PGA Volume",   0x87, 4,  3, 0, lrpga2lrhp_tlv),
	SOC_SINGLE_TLV("LPGA2RHP PGA Volume",   0x87, 2,  3, 0, lrpga2lrhp_tlv),
	SOC_SINGLE_TLV("RPGA2RHP PGA Volume",   0x87, 0,  3, 0, lrpga2lrhp_tlv),
	SOC_SINGLE_TLV("LSPKPreMixer PGA Volume",  0x8e, 0, 46, 0, lrspkpga_tlv),
	SOC_SINGLE_TLV("RSPKPreMixer PGA Volume",  0x8f, 0, 46, 0, lrspkpga_tlv),
	SOC_SINGLE_TLV("LHPPreMixer PGA Volume",   0x88, 0, 31, 0, lrhppga_tlv),
	SOC_SINGLE_TLV("RHPPreMixer PGA Volume",   0x89, 0, 31, 0, lrhppga_tlv),
	SOC_SINGLE_TLV("RECVDrv Volume", 0x84, 3, 1, 0, recvoutdrv_tlv), 
	SOC_SINGLE_TLV("LSPKDrv Volume", 0x90, 4, 3, 0, spkoutdrv_tlv),
	SOC_SINGLE_TLV("RSPKDrv Volume", 0x90, 0, 3, 0, spkoutdrv_tlv),
	SOC_SINGLE_TLV("LHPDrv Volume", 0x8a, 0, 15, 0, hpoutdrv_tlv),
	SOC_SINGLE_TLV("RHPDrv Volume", 0x8b, 0, 15, 0, hpoutdrv_tlv),
	SOC_DOUBLE_TLV("AUD_ADC1 PGA Volume", 0x0c, 17, 1, 511, 1, digital_tlv),
	SOC_DOUBLE_TLV("AUD_ADC2 PGA Volume", 0x0d, 17, 1, 511, 1, digital_tlv),
	SOC_DOUBLE_TLV("AUD_DAC1 PGA Volume", 0x0a, 17, 1, 511, 1, digital_tlv),
	SOC_DOUBLE_TLV("AUD_DAC2 PGA Volume", 0x0b, 17, 1, 511, 1, digital_tlv),
	SOC_DOUBLE_TLV("Sidetone PGA Volume", 0x0e, 17, 1, 511, 1, digital_tlv),
	SOC_ENUM("AIF2L2DACL sel", rt5512_enum[3]),
	SOC_ENUM("AIF1L2DACL sel", rt5512_enum[4]),
	SOC_ENUM("ADCL2DACL sel",  rt5512_enum[5]),
	SOC_ENUM("ADCR2DACL sel",  rt5512_enum[6]),
	SOC_ENUM("AIF2R2DACR sel", rt5512_enum[7]),
	SOC_ENUM("AIF1R2DACR sel", rt5512_enum[8]),
	SOC_ENUM("ADCL2DACR sel",  rt5512_enum[9]),
	SOC_ENUM("ADCR2DACR sel",  rt5512_enum[10]),
	SOC_ENUM("AIF2L2TX2L sel", rt5512_enum[11]),
	SOC_ENUM("AIF1L2TX2L sel", rt5512_enum[12]),
	SOC_ENUM("ADCL2TX2L sel",  rt5512_enum[13]),
	SOC_ENUM("ADCR2TX2L sel",  rt5512_enum[14]),
	SOC_ENUM("AIF2R2TX2R sel", rt5512_enum[15]),
	SOC_ENUM("AIF1R2TX2R sel", rt5512_enum[16]),
	SOC_ENUM("ADCL2TX2R sel",  rt5512_enum[17]),
	SOC_ENUM("ADCR2TX2R sel",  rt5512_enum[18]),
	SOC_SINGLE("ADC_HPF1 Switch", 0x16, 1, 1, 0),
	SOC_ENUM("ADC_HPF1 FC", rt5512_enum[25]),
	SOC_SINGLE("ADC_HPF2 Switch", 0x16, 2, 1, 0),
	SOC_ENUM("ADC_HPF2 FC", rt5512_enum[26]),
	SOC_SINGLE("ADC_HPF3 Switch", 0x16, 3, 1, 0),
	SOC_ENUM("ADC_HPF3 FC", rt5512_enum[27]),
	SOC_SINGLE("ADC_DRC1 Switch", 0x16, 4, 1, 0),
	SOC_SINGLE("ADC_DRC2 Switch", 0x16, 5, 1, 0),
	SOC_SINGLE("ADC_DRC NG Switch", 0x31, 0, 1, 0),
	SOC_SINGLE("DAC_3D1 Switch", 0x14, 0, 1, 0),
	SOC_SINGLE("DAC_3D2 Switch", 0x14, 1, 1, 0),
	SOC_SINGLE("DAC_DRC1 Switch", 0x15, 2, 1, 0),
	SOC_SINGLE("DAC_DRC2 Switch", 0x15, 3, 1, 0),
	SOC_SINGLE("DAC_DRC3 Switch", 0x15, 4, 1, 0),
	SOC_SINGLE("DAC_DRC4 Switch", 0x15, 5, 1, 0),
	SOC_SINGLE("DAC_DRC NG Switch", 0x31, 1, 1, 0),
	SOC_SINGLE("DAC_BASS1 Switch", 0x14, 2, 1, 0),
	SOC_SINGLE("DAC_BASS2 Switch", 0x14, 3, 1, 0),
	SOC_SINGLE("DAC_EQ1 Switch", 0x15, 0, 1, 0),
	SOC_SINGLE("DAC_EQ2 Switch", 0x15, 1, 1, 0),
#if FOR_MID
    SOC_ENUM_EXT("Playback Path", rt5512_path_type,
		rt5512_playback_path_get, rt5512_playback_path_put),		
	SOC_ENUM_EXT("Capture MIC Path", rt5512_capture_type,
		rt5512_capture_path_get, rt5512_capture_path_put),
#else
    SOC_ENUM_EXT("Out Sel", rt5512_enum[23], rt5512_outpath_get, rt5512_outpath_set),
    SOC_ENUM_EXT("In Sel", rt5512_enum[24], rt5512_inpath_get, rt5512_inpath_set),
#endif
};

static const struct snd_kcontrol_new linput_mixer_controls[] =
{
	SOC_DAPM_SINGLE("AuxL Switch", 0x95, 7, 1, 0),
	SOC_DAPM_SINGLE("Mic1 Switch", 0x95, 6, 1, 0),
	SOC_DAPM_SINGLE("Mic2 Switch", 0x95, 5, 1, 0),
	SOC_DAPM_SINGLE("Mic3 Switch", 0x95, 4, 1, 0),
};

static const struct snd_kcontrol_new rinput_mixer_controls[] =
{
	SOC_DAPM_SINGLE("AuxR Switch", 0x95, 3, 1, 0),
	SOC_DAPM_SINGLE("Mic1 Switch", 0x95, 2, 1, 0),
	SOC_DAPM_SINGLE("Mic2 Switch", 0x95, 1, 1, 0),
	SOC_DAPM_SINGLE("Mic3 Switch", 0x95, 0, 1, 0),
};

static const struct snd_kcontrol_new analog_bypass_controls =
	SOC_DAPM_ENUM("Analog Bypass", rt5512_enum[2]);

static const struct snd_kcontrol_new lspkpre_mixer_controls[] =
{
	SOC_DAPM_SINGLE("LPGA Switch", 0x8c, 7, 1, 0),
	SOC_DAPM_SINGLE("LSPKDAC Switch", 0x84, 1, 1, 0),
	SOC_DAPM_SINGLE("RPGA Switch", 0x8c, 6, 1, 0),
};

static const struct snd_kcontrol_new rspkpre_mixer_controls[] =
{
	SOC_DAPM_SINGLE("LPGA Switch", 0x8c, 5, 1, 0),
	SOC_DAPM_SINGLE("RSPKDAC Switch", 0x84, 0, 1, 0),
	SOC_DAPM_SINGLE("RPGA Switch", 0x8c, 4, 1, 0),
};

static const struct snd_kcontrol_new recvout_mixer_controls[] =
{
	SOC_DAPM_SINGLE("LSPKPGA Switch", 0x84, 6, 1, 0),
	SOC_DAPM_SINGLE("RSPKPGA Switch", 0x84, 5, 1, 0),
	SOC_DAPM_SINGLE("BYPASS Switch",  0x84, 4, 1, 0),
};

static const struct snd_kcontrol_new lspkpost_mixer_controls[] =
{
	SOC_DAPM_SINGLE("RSPKPGA Switch", 0x8c, 2, 1, 0),
	SOC_DAPM_SINGLE("LSPKPGA Switch", 0x8c, 3, 1, 0),
	SOC_DAPM_SINGLE("BYPASS Switch",  0x90, 7, 1, 0),
};

static const struct snd_kcontrol_new rspkpost_mixer_controls[] =
{
	SOC_DAPM_SINGLE("LSPKPGA Switch", 0x8c, 1, 1, 0),
	SOC_DAPM_SINGLE("RSPKPGA Switch", 0x8c, 0, 1, 0),
	SOC_DAPM_SINGLE("BYPASS Switch",  0x90, 3, 1, 0),
};

static const struct snd_kcontrol_new lhppre_mixer_controls[] =
{
	SOC_DAPM_SINGLE("LPGA Switch", 0x86, 7, 1, 0),
	SOC_DAPM_SINGLE("RPGA Switch", 0x86, 6, 1, 0),
};

static const struct snd_kcontrol_new rhppre_mixer_controls[] =
{
	SOC_DAPM_SINGLE("LPGA Switch", 0x86, 5, 1, 0),
	SOC_DAPM_SINGLE("RPGA Switch", 0x86, 4, 1, 0),
};

static const struct snd_kcontrol_new lhppost_mixer_controls[] =
{
	SOC_DAPM_SINGLE("LHPDAC Switch", RT5512_VIRTUAL_REG, 6, 1, 0),
	SOC_DAPM_SINGLE("BYPASS Switch", 0x86, 1, 1, 0),
	SOC_DAPM_SINGLE("LRPGAMixer Switch",  RT5512_VIRTUAL_REG, 7, 1, 0),
};

static const struct snd_kcontrol_new rhppost_mixer_controls[] =
{
	SOC_DAPM_SINGLE("LRPGAMixer Switch", RT5512_VIRTUAL_REG, 8, 1, 0),
	SOC_DAPM_SINGLE("BYPASS Switch", 0x86, 0, 1, 0),
	SOC_DAPM_SINGLE("RHPDAC Switch",  RT5512_VIRTUAL_REG, 9, 1, 0),
};

static int rt5512_power_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event)
{
	RT_DBG("\n");
	switch (event)
	{
		case SND_SOC_DAPM_PRE_PMU:
			snd_soc_update_bits(w->codec, 0x81, 0x7, 0x7);
			mdelay(20);
			snd_soc_update_bits(w->codec, 0x81, 0x4, 0x0);
			mdelay(1);
			break;
		case SND_SOC_DAPM_POST_PMD:
			snd_soc_update_bits(w->codec, 0x81, 0x7, 0x4);
			break;
	}
	return 0;
}

static int rt5512_lhpdrv_pevent(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event)
{
	RT_DBG("\n");
	switch (event)
	{
		case SND_SOC_DAPM_PRE_PMU:
			snd_soc_update_bits(w->codec, 0x83, 0x80, 0x80);
			mdelay(1);
			break;
        case SND_SOC_DAPM_PRE_PMD:
            snd_soc_update_bits(w->codec, 0x8A, 0x0F, 0x00);
            mdelay(50);
            break;
		case SND_SOC_DAPM_POST_PMD:
			mdelay(10);
			snd_soc_update_bits(w->codec, 0x83, 0x80, 0x0);
            mdelay(10);
            snd_soc_update_bits(w->codec, 0x8A, 0x0F, 0x0B);
			break;
	}
	return 0;
}

static int rt5512_rhpdrv_pevent(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event)
{
	RT_DBG("\n");
	switch (event)
	{
		case SND_SOC_DAPM_PRE_PMU:
			snd_soc_update_bits(w->codec, 0x83, 0x80, 0x80);
			mdelay(1);
			break;
        case SND_SOC_DAPM_PRE_PMD:
            snd_soc_update_bits(w->codec, 0x8B, 0x0F, 0x00);
            mdelay(50);
            break;
		case SND_SOC_DAPM_POST_PMD:
			mdelay(10);
            snd_soc_update_bits(w->codec, 0x83, 0x80, 0x0);
            mdelay(10);
            snd_soc_update_bits(w->codec, 0x8B, 0x0F, 0x0B);
			break;
	}
	return 0;
}

static int rt5512_spkdac_pevent(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event)
{
	RT_DBG("\n");
	switch (event)
	{
		case SND_SOC_DAPM_POST_PMU:
			mdelay(125);
			break;
	}
	return 0;
}

static int rt5512_spkdrv_pevent(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event)
{
	RT_DBG("\n");
	switch (event)
	{
		case SND_SOC_DAPM_PRE_PMU:
			mdelay(1);
			break;
		case SND_SOC_DAPM_POST_PMD:
			mdelay(1);
			break;
	}
	return 0;
}

static int rt5512_recvdrv_pevent(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event)
{
	RT_DBG("\n");
	switch (event)
	{
		case SND_SOC_DAPM_PRE_PMU:
			snd_soc_update_bits(w->codec, 0x83, 0x80, 0x80);
			mdelay(2);
			break;
		case SND_SOC_DAPM_POST_PMD:
			mdelay(1);
			snd_soc_update_bits(w->codec, 0x83, 0x80, 0x0);
			mdelay(1);
			break;
	}
	return 0;
}

static const struct snd_soc_dapm_widget rt5512_dapm_widgets[] =
{
	SND_SOC_DAPM_PRE("Pre Power", rt5512_power_event),
	SND_SOC_DAPM_POST("Post Power",rt5512_power_event),
	// Input Related
	// Analog Input Pin
	SND_SOC_DAPM_INPUT("Mic1"),
	SND_SOC_DAPM_INPUT("Mic2"),
	SND_SOC_DAPM_INPUT("Mic3"),
	SND_SOC_DAPM_INPUT("Aux"),
	// Analog Bypass
	SND_SOC_DAPM_MUX("Analog Bypass Mux", RT5512_VIRTUAL_REG, 0, 0, &analog_bypass_controls),
	// Input PGA
	SND_SOC_DAPM_PGA("Aux PGA",  0x85, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mic1 PGA", 0x85, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mic2 PGA", 0x85, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mic3 PGA", 0x85, 0, 0, NULL, 0),
	// Input Mixer
	SND_SOC_DAPM_MIXER("LInput Mixer", 0x85, 5, 0, linput_mixer_controls, ARRAY_SIZE(linput_mixer_controls)),
	SND_SOC_DAPM_MIXER("RInput Mixer", 0x85, 4, 0, rinput_mixer_controls, ARRAY_SIZE(rinput_mixer_controls)),
	// Input ADC
	SND_SOC_DAPM_ADC("LADC", NULL, 0x85, 7, 0),
	SND_SOC_DAPM_ADC("RADC", NULL, 0x85, 6, 0),
	// Digital Input Interface
	SND_SOC_DAPM_AIF_OUT("AIF1ADC", NULL, 0, 0x06, 0, 0),
	//SND_SOC_DAPM_AIF_OUT("AIF1ADCR", NULL, 0, 0x06, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1ADCDAT", "AIF1 Capture", 0, 0x05, 2, 1),
	SND_SOC_DAPM_AIF_OUT("AIF2ADC", NULL, 0, 0x06, 1, 0),
	//SND_SOC_DAPM_AIF_OUT("AIF2ADCR", NULL, 0, 0x06, 1, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2ADCDAT", "AIF2 Capture", 0, 0x05, 3, 1),

	//Digital DSP Routing
	//Mixer
	SND_SOC_DAPM_MIXER("DACL Mixer", 0x07, 7, 1, NULL, 0),
	SND_SOC_DAPM_MIXER("DACR Mixer", 0x07, 6, 1, NULL, 0),
	SND_SOC_DAPM_MIXER("TX2L Mixer", 0x07, 5, 1, NULL, 0),
	SND_SOC_DAPM_MIXER("TX2R Mixer", 0x07, 4, 1, NULL, 0),

	// Output Related
	SND_SOC_DAPM_AIF_IN("AIF1DAC", NULL, 0, 0x06, 2, 0),
	//SND_SOC_DAPM_AIF_IN("AIF1DACR", NULL, 0, 0x06, 2, 0),
	SND_SOC_DAPM_AIF_IN("AIF1DACDAT", "AIF1 Playback", 0, 0x05, 0, 1),
	SND_SOC_DAPM_AIF_IN("AIF2DAC", NULL, 0, 0x06, 3, 0),
	//SND_SOC_DAPM_AIF_IN("AIF2DACR", NULL, 0, 0x06, 3, 0),
	SND_SOC_DAPM_AIF_IN("AIF2DACDAT", "AIF2 Playback", 0, 0x05, 1, 1),
	// Output DAC
	SND_SOC_DAPM_DAC_E("LSPKDAC", NULL, 0x82, 1, 0, rt5512_spkdac_pevent,
 		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_DAC("RSPKDAC", NULL, 0x82, 0, 0),
	SND_SOC_DAPM_DAC("LHPDAC",  NULL, 0x82, 3, 0),
	SND_SOC_DAPM_DAC("RHPDAC",  NULL, 0x82, 2, 0),
	// Output Mixer
	SND_SOC_DAPM_MIXER("LSPK Pre Mixer", 0x82, 5, 0, lspkpre_mixer_controls, ARRAY_SIZE(lspkpre_mixer_controls)),
	SND_SOC_DAPM_MIXER("RSPK Pre Mixer", 0x82, 4, 0, rspkpre_mixer_controls, ARRAY_SIZE(rspkpre_mixer_controls)),
	SND_SOC_DAPM_MIXER("RECV Mixer", RT5512_VIRTUAL_REG, 1, 0, recvout_mixer_controls, ARRAY_SIZE(recvout_mixer_controls)),
	SND_SOC_DAPM_MIXER("LSPK Post Mixer", RT5512_VIRTUAL_REG, 2, 0, lspkpost_mixer_controls, ARRAY_SIZE(lspkpost_mixer_controls)),
	SND_SOC_DAPM_MIXER("RSPK Post Mixer", RT5512_VIRTUAL_REG, 3, 0, rspkpost_mixer_controls, ARRAY_SIZE(rspkpost_mixer_controls)),
	SND_SOC_DAPM_MIXER("LHP Pre Mixer", 0x82, 7, 0, lhppre_mixer_controls, ARRAY_SIZE(lhppre_mixer_controls)),
	SND_SOC_DAPM_MIXER("RHP Pre Mixer", 0x82, 6, 0, rhppre_mixer_controls, ARRAY_SIZE(rhppre_mixer_controls)),
	SND_SOC_DAPM_MIXER("LHP Post Mixer", RT5512_VIRTUAL_REG, 4, 0, lhppost_mixer_controls, ARRAY_SIZE(lhppost_mixer_controls)),
	SND_SOC_DAPM_MIXER("RHP Post Mixer", RT5512_VIRTUAL_REG, 5, 0, rhppost_mixer_controls, ARRAY_SIZE(rhppost_mixer_controls)),
	// Output Drv
	SND_SOC_DAPM_OUT_DRV_E("RECV Drv", 0x84, 7, 0, NULL, 0, rt5512_recvdrv_pevent,
		SND_SOC_DAPM_PRE_PMU|SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUT_DRV_E("LSPK Drv", 0x83, 3, 0, NULL, 0, rt5512_spkdrv_pevent,
		SND_SOC_DAPM_PRE_PMU|SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUT_DRV_E("RSPK Drv", 0x83, 2, 0, NULL, 0, rt5512_spkdrv_pevent,
		SND_SOC_DAPM_PRE_PMU|SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUT_DRV_E("LHP Drv", 0x83, 1, 0, NULL, 0, rt5512_lhpdrv_pevent,
		SND_SOC_DAPM_PRE_PMU|SND_SOC_DAPM_PRE_PMD|SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUT_DRV_E("RHP Drv", 0x83, 0, 0, NULL, 0, rt5512_rhpdrv_pevent,
		SND_SOC_DAPM_PRE_PMU|SND_SOC_DAPM_PRE_PMD||SND_SOC_DAPM_POST_PMU),
	// Output Pin
	SND_SOC_DAPM_OUTPUT("Receiver"),
	SND_SOC_DAPM_OUTPUT("LSpeaker"),
	SND_SOC_DAPM_OUTPUT("RSpeaker"),
	SND_SOC_DAPM_OUTPUT("LHeadphone"),
	SND_SOC_DAPM_OUTPUT("RHeadphone"),
	//MicBias
	SND_SOC_DAPM_MICBIAS("MicBias1", 0x81, 5, 0),
	SND_SOC_DAPM_MICBIAS("MicBias2", 0x81, 5, 0),
};

static const struct snd_soc_dapm_route rt5512_dapm_routes[] = 
{
	// Input
	{"Mic1 PGA", NULL, "Mic1"},
	{"Mic2 PGA", NULL, "Mic2"},
	{"Mic3 PGA", NULL, "Mic3"},
	{"Aux PGA",  NULL, "Aux"},
	{"Analog Bypass Mux", "Mic1", "Mic1"},
	{"Analog Bypass Mux", "Mic2", "Mic2"},
	{"Analog Bypass Mux", "Mic3", "Mic3"},
	{"Analog Bypass Mux", "Aux",  "Aux"},
	{"LInput Mixer", "Mic1 Switch", "Mic1 PGA"},
	{"LInput Mixer", "Mic2 Switch", "Mic2 PGA"},
	{"LInput Mixer", "Mic3 Switch", "Mic3 PGA"},
	{"LInput Mixer", "AuxL Switch", "Aux PGA"},
	{"RInput Mixer", "Mic1 Switch", "Mic1 PGA"},
	{"RInput Mixer", "Mic2 Switch", "Mic2 PGA"},
	{"RInput Mixer", "Mic3 Switch", "Mic3 PGA"},
	{"RInput Mixer", "AuxR Switch", "Aux PGA"},
	{"LADC", NULL, "LInput Mixer"},
	{"RADC", NULL, "RInput Mixer"},
	{"AIF1ADC", NULL, "LADC"},
	{"AIF1ADC", NULL, "RADC"},
	{"AIF1ADCDAT", NULL, "AIF1ADC"},
	//{"AIF1ADCDAT", NULL, "AIF1ADCR"},
	{"AIF2ADC", NULL, "TX2L Mixer"},
	{"AIF2ADC", NULL, "TX2R Mixer"},
	{"AIF2ADCDAT", NULL, "AIF2ADC"},
	//{"AIF2ADCDAT", NULL, "AIF2ADCR"},
	// Output
	{"AIF1DAC", NULL, "AIF1DACDAT"},
	//{"AIF1DACR", NULL, "AIF1DACDAT"},
	{"AIF2DAC", NULL, "AIF2DACDAT"},
	//{"AIF2DACR", NULL, "AIF2DACDAT"},
	{"DACL Mixer", NULL, "AIF1DAC"},
	{"DACL Mixer", NULL, "AIF2DAC"},
	{"DACL Mixer", NULL, "LADC"},
	{"DACL Mixer", NULL, "RADC"},
	{"DACR Mixer", NULL, "AIF1DAC"},
	{"DACR Mixer", NULL, "AIF2DAC"},
	{"DACR Mixer", NULL, "LADC"},
	{"DACR Mixer", NULL, "RADC"},
	{"TX2L Mixer", NULL, "AIF1DAC"},
	{"TX2L Mixer", NULL, "AIF2DAC"},
	{"TX2L Mixer", NULL, "LADC"},
	{"TX2L Mixer", NULL, "RADC"},
	{"TX2R Mixer", NULL, "AIF1DAC"},
	{"TX2R Mixer", NULL, "AIF2DAC"},
	{"TX2R Mixer", NULL, "LADC"},
	{"TX2R Mixer", NULL, "RADC"},
	{"LSPKDAC", NULL, "DACL Mixer"},
	{"RSPKDAC", NULL, "DACR Mixer"},
	{"LHPDAC", NULL, "DACL Mixer"},
	{"RHPDAC", NULL, "DACR Mixer"},
	{"LSPK Pre Mixer", "LPGA Switch", "LInput Mixer"},
	{"LSPK Pre Mixer", "LSPKDAC Switch", "LSPKDAC"},
	{"LSPK Pre Mixer", "RPGA Switch", "RInput Mixer"},
	{"RSPK Pre Mixer", "LPGA Switch", "LInput Mixer"},
	{"RSPK Pre Mixer", "RSPKDAC Switch", "RSPKDAC"},
	{"RSPK Pre Mixer", "RPGA Switch", "RInput Mixer"},
	{"LHP Pre Mixer", "LPGA Switch", "LInput Mixer"},
	{"LHP Pre Mixer", "RPGA Switch", "RInput Mixer"},
	{"RHP Pre Mixer", "LPGA Switch", "LInput Mixer"},
	{"RHP Pre Mixer", "RPGA Switch", "RInput Mixer"},
	{"RECV Mixer", "LSPKPGA Switch", "LSPK Pre Mixer"},
	{"RECV Mixer", "RSPKPGA Switch", "RSPK Pre Mixer"},
	{"RECV Mixer", "BYPASS Switch", "Analog Bypass Mux"},
	{"LSPK Post Mixer", "RSPKPGA Switch", "RSPK Pre Mixer"},
	{"LSPK Post Mixer", "LSPKPGA Switch", "LSPK Pre Mixer"},
	{"LSPK Post Mixer", "BYPASS Switch", "Analog Bypass Mux"},
	{"RSPK Post Mixer", "LSPKPGA Switch", "LSPK Pre Mixer"},
	{"RSPK Post Mixer", "RSPKPGA Switch", "RSPK Pre Mixer"},
	{"RSPK Post Mixer", "BYPASS Switch", "Analog Bypass Mux"},
	{"LHP Post Mixer", "LHPDAC Switch", "LHPDAC"},
	{"LHP Post Mixer", "BYPASS Switch", "Analog Bypass Mux"},
	{"LHP Post Mixer", "LRPGAMixer Switch", "LHP Pre Mixer"},
	{"RHP Post Mixer", "LRPGAMixer Switch", "RHP Pre Mixer"},
	{"RHP Post Mixer", "BYPASS Switch", "Analog Bypass Mux"},
	{"RHP Post Mixer", "RHPDAC Switch", "RHPDAC"},
	{"RECV Drv", NULL, "RECV Mixer"},
	{"LSPK Drv", NULL, "LSPK Post Mixer"},
	{"RSPK Drv", NULL, "RSPK Post Mixer"},
	{"LHP Drv", NULL, "LHP Post Mixer"},
	{"RHP Drv", NULL, "RHP Post Mixer"},
	{"Receiver", NULL, "RECV Drv"},
	{"LSpeaker", NULL, "LSPK Drv"},
	{"RSpeaker", NULL, "RSPK Drv"},
	{"LHeadphone", NULL, "LHP Drv"},
	{"RHeadphone", NULL, "RHP Drv"},
};

void	odroid_audio_tvout(bool tvout)
{
	return;
}


static int rt5512_id_check(struct snd_soc_codec *codec)
{
    int ret = -1;
    u8 regaddr = 0x00; // always revision id register
    u8 regval;
    
    ret = i2c_master_send(codec->control_data, &regaddr, 1);
	if (ret != 1)
	{
		dev_err(codec->dev, "send reg addr fail\n");
		return -EIO;
	}
	ret = i2c_master_recv(codec->control_data, &regval, 1);
	if (ret != 1)
	{
		dev_err(codec->dev, "read regval fail\n");
		return -EIO;
	}
    
    return regval;
}
static unsigned int rt5512_io_read(struct snd_soc_codec *codec, unsigned int reg)
{
	int ret;
	char regaddr = reg&0xff;  // 8bit register address
	char regval[4] = {0};
	int len = rt5512_reg_priv[reg].reg_size;
	int i;

	RT_DBG("reg 0x%02x\n", reg);
	if (reg == RT5512_VIRTUAL_REG)
	{
		return rt5512_reg_priv[reg].reg_cache;
	}

	if (len == 8 || len == 16 || len == 20)
	{
		pr_err("rt5512-codec: not supported reg data size in this read function\n");
		return -EINVAL;
	}
	else if (len == 0)
	{
		dev_err(codec->dev, "Invalid reg_addr 0x%02x\n", reg);
		return -EINVAL;
	}

	if (rt5512_reg_priv[reg].sync_needed)
	{
		ret = i2c_master_send(codec->control_data, &regaddr, 1);
		if (ret != 1)
		{
			dev_err(codec->dev, "send reg addr fail\n");
			return -EIO;
		}
		ret = i2c_master_recv(codec->control_data, regval, len);
		if (ret != len)
		{
			dev_err(codec->dev, "read regval fail\n");
			return -EIO;
		}

		switch (len)
		{
			case 1:
				ret = 0x00 | regval[0];
				break;
			case 4:
				for (i=0, ret=0x0; i<4 ; i++)
					ret |= (regval[i] << (24-8*i));
				break;
		}
		rt5512_reg_priv[reg].reg_cache = ret;
		rt5512_reg_priv[reg].sync_needed = 0;
	}
	else
		ret = rt5512_reg_priv[reg].reg_cache;

	return ret;
}

static int rt5512_io_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int regval)
{
	int ret;
	char data[5] = {0};
	int len = rt5512_reg_priv[reg].reg_size;
	int i;

	RT_DBG("reg 0x%02x, val 0x%08x\n", reg, regval);
	if (reg == RT5512_VIRTUAL_REG)
	{
		rt5512_reg_priv[reg].reg_cache = regval;
		return 0;
	}

	if (len == 8 || len == 16 || len == 20)
	{
		pr_err("rt5512-codec: not supported reg data size in this write function\n");
		return -EINVAL;
	}
	else if (len == 0)
	{
		dev_err(codec->dev, "Invalid reg_addr 0x%02x\n", reg);
		return -EINVAL;
	}

	data[0] = reg&0xff;

	switch (len)
	{
		case 1:
			data[1] = regval&0xff;
			break;
		case 4:
			for (i=1; i<5; i++)
				data[i] = (regval>>(32-8*i))&0xff;
			break;
	}

	ret = i2c_master_send(codec->control_data, data, 1+len);
	if (ret != (1+len))
	{
		dev_err(codec->dev, "write register fail\n");
		return -EIO;
	}
	rt5512_reg_priv[reg].sync_needed = 1;
	return 0;
}

static int rt5512_set_codec_io(struct snd_soc_codec *codec)
{
	struct rt5512_codec_chip *chip = snd_soc_codec_get_drvdata(codec);

	RT_DBG("\n");
	codec->read = rt5512_io_read;
	codec->write = rt5512_io_write;
	codec->control_data = chip->client;
	return 0;
}

static int rt5512_digital_block_power(struct snd_soc_codec *codec, int pd)
{
	RT_DBG("powerdown = %d\n", pd);
	snd_soc_update_bits(codec, 0x80, RT5512_PDBIT_MASK, (pd?RT5512_PDBIT_MASK:0));
	return 0;
}

static int rt5512_set_bias_level(struct snd_soc_codec *codec,
			      enum snd_soc_bias_level level)
{
	RT_DBG("bias_level->%d\n",(int)level);
	if (codec->dapm.bias_level != level)
	{
		switch (level)
		{
			case SND_SOC_BIAS_ON:
			case SND_SOC_BIAS_PREPARE:
			case SND_SOC_BIAS_STANDBY:
				rt5512_digital_block_power(codec, 0);
				break;
			case SND_SOC_BIAS_OFF:
				rt5512_digital_block_power(codec, 1);
				break;
			default:
				dev_err(codec->dev, "not supported bias level\n");
		}
		//snd_soc_write(codec, reg, value);
		codec->dapm.bias_level = level;
	}
	else
		dev_warn(codec->dev, "the same bias level, no need to change\n");
	return 0;
}

static void rt5512_codec_reset(struct snd_soc_codec *codec)
{
	RT_DBG("\n");
	snd_soc_update_bits(codec, 0x80, RT5512_RSTBIT_MASK, RT5512_RSTBIT_MASK);
}

static void rt5512_codec_force_reload_cache(struct snd_soc_codec *codec)
{
	int i;
	int ret;
	RT_DBG("\n");
	for (i=0; i<RT5512_MAX_REG; i++)
	{
		if (rt5512_reg_priv[i].reg_size == 1 || rt5512_reg_priv[i].reg_size == 4)
		{
			rt5512_reg_priv[i].sync_needed = 1;
			ret = snd_soc_read(codec, i);
			rt5512_reg_priv[i].reg_cache = ret;
		}
	}
}

static int rt5512_codec_probe(struct snd_soc_codec *codec)
{
	RT_DBG("\n");
  //  unsigned int val1,val2;
	rt5512_set_codec_io(codec);

    if (rt5512_id_check(codec) != 0x20)
        return -EINVAL;
    printk("read codec chip id is 0x%x\n",rt5512_id_check(codec));

    rt5512_codec_reset(codec);
	rt5512_codec_force_reload_cache(codec);

#if FOR_MID
    rt5512_reg_init(codec);
#endif
	rt5512_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
        
    

	snd_soc_add_controls(codec, rt5512_snd_controls,
			ARRAY_SIZE(rt5512_snd_controls));
	return 0;
}

static int rt5512_codec_remove(struct snd_soc_codec *codec)
{
	RT_DBG("\n");
	rt5512_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

#ifdef CONFIG_PM
static int rt5512_codec_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	RT_DBG("\n");
	rt5512_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int rt5512_codec_resume(struct snd_soc_codec *codec)
{
	RT_DBG("\n");
	rt5512_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#else
#define rt5512_codec_suspend NULL
#define rt5512_codec_resume  NULL
#endif /* #ifdef CONFIG_PM */

static struct snd_soc_codec_driver soc_codec_dev_rt5512 = 
{
	.probe =	rt5512_codec_probe,
	.remove =	rt5512_codec_remove,
	.suspend =	rt5512_codec_suspend,
	.resume =	rt5512_codec_resume,
	.set_bias_level = rt5512_set_bias_level,
	//.reg_cache_size = ARRAY_SIZE(rt5512_reg),
	//.reg_word_size = sizeof(u32),
	//.reg_cache_default = rt5512_reg,
	.dapm_widgets = rt5512_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5512_dapm_widgets),
	.dapm_routes = rt5512_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5512_dapm_routes),
};

#ifdef CONFIG_DEBUG_FS
static struct i2c_client *this_client;
static struct dentry *debugfs_rt_dent;
static struct dentry *debugfs_peek;
static struct dentry *debugfs_poke;
static struct dentry *debugfs_headset_test;

static unsigned char read_data[20];
static unsigned int read_size;

static int codec_debug_open(struct inode *inode, struct file *file)
{
	RT_DBG("\n");

	file->private_data = inode->i_private;
	return 0;
}

static int get_parameters(char *buf, long int *param1, int num_of_par)
{
	char *token;
	int base, cnt;

	RT_DBG("\n");

	token = strsep(&buf, " ");

	for (cnt = 0; cnt < num_of_par; cnt++) {
		if (token != NULL) {
			if ((token[1] == 'x') || (token[1] == 'X'))
				base = 16;
			else
				base = 10;

			if (strict_strtoul(token, base, &param1[cnt]) != 0)
				return -EINVAL;

			token = strsep(&buf, " ");
			}
		else
			break;
	}
	return cnt;
}

#define STR_4TIMES "0x%02x 0x%02x 0x%02x 0x%02x "

static ssize_t codec_debug_read(struct file *filp, char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char lbuf[200];

	RT_DBG("\n");
	
	switch (read_size)
	{
		case 1:
			snprintf(lbuf, sizeof(lbuf), "0x%02x\n", read_data[0]);
			break;
		case 4:
			snprintf(lbuf, sizeof(lbuf), STR_4TIMES "\n",
				read_data[0], read_data[1], read_data[2], read_data[3]);
			break;
		case 8:
			snprintf(lbuf, sizeof(lbuf), STR_4TIMES STR_4TIMES "\n",
				read_data[0], read_data[1], read_data[2], read_data[3],
				read_data[4], read_data[5], read_data[6], read_data[7]);
			break;
		case 16:
			snprintf(lbuf, sizeof(lbuf), STR_4TIMES STR_4TIMES STR_4TIMES STR_4TIMES "\n",
				read_data[0], read_data[1], read_data[2], read_data[3],
				read_data[4], read_data[5], read_data[6], read_data[7],
				read_data[8], read_data[9], read_data[10], read_data[11],
				read_data[12], read_data[13], read_data[14], read_data[15]);
			break;
		case 20:
			snprintf(lbuf, sizeof(lbuf), STR_4TIMES STR_4TIMES STR_4TIMES STR_4TIMES STR_4TIMES "\n",
				read_data[0], read_data[1], read_data[2], read_data[3],
				read_data[4], read_data[5], read_data[6], read_data[7],
				read_data[8], read_data[9], read_data[10], read_data[11],
				read_data[12], read_data[13], read_data[14], read_data[15],
				read_data[16], read_data[17], read_data[18], read_data[19]);
			break;
		default:
			return 0;
	}
	return simple_read_from_buffer(ubuf, count, ppos, lbuf, strlen(lbuf));
}

static int rt5512_simulate_headset(struct i2c_client *client, int id)
{
	struct rt5512_codec_chip *chip = i2c_get_clientdata(client);

	RT_DBG("id = %d\n", id);
	switch (id)
	{
		case 0: // simulate headset insert
			snd_soc_jack_report(chip->rt_jack, SND_JACK_HEADSET, SND_JACK_HEADSET);
			break;
		case 1: // simulate headset plug out
			snd_soc_jack_report(chip->rt_jack, 0, SND_JACK_HEADSET);
			break;
		case 2: // simulate headphone(no microphone) insert
			snd_soc_jack_report(chip->rt_jack, SND_JACK_HEADPHONE, SND_JACK_HEADPHONE);
			break;
		case 3: // simulate headphone(no microphone) plug out
			snd_soc_jack_report(chip->rt_jack, 0, SND_JACK_HEADPHONE);
			break;
		case 4: // simulate headset jack btn 0 click
			snd_soc_jack_report(chip->rt_jack, SND_JACK_BTN_0, SND_JACK_BTN_0);
			mdelay(1);
			snd_soc_jack_report(chip->rt_jack, 0, SND_JACK_BTN_0);
			break;
		case 5: // simulate headset jack btn 1 click
			snd_soc_jack_report(chip->rt_jack, SND_JACK_BTN_1, SND_JACK_BTN_1);
			mdelay(1);
			snd_soc_jack_report(chip->rt_jack, 0, SND_JACK_BTN_1);
			break;
		case 6: // simulate headset jack btn 2 click
			snd_soc_jack_report(chip->rt_jack, SND_JACK_BTN_2, SND_JACK_BTN_2);
			mdelay(1);
			snd_soc_jack_report(chip->rt_jack, 0, SND_JACK_BTN_2);
			break;
	}
	return 0;
}

static ssize_t codec_debug_write(struct file *filp,
	const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char *access_str = filp->private_data;
	char lbuf[200];
	int rc;
	long int param[21];
	int i = 0;
	int reg_dsize = 0;

	RT_DBG("\n");

	if (cnt > sizeof(lbuf) - 1)
		return -EINVAL;

	rc = copy_from_user(lbuf, ubuf, cnt);
	if (rc)
		return -EFAULT;

	lbuf[cnt] = '\0';

	if (!strcmp(access_str, "poke")) {
		/* write */
		rc = get_parameters(lbuf, param, 21);
		if (rc > 0 && param[0]<=RT5512_MAX_REG && rc == (rt5512_reg_priv[param[0]].reg_size+1))
		{
			reg_dsize = rt5512_reg_priv[param[0]].reg_size;
			switch (reg_dsize)
			{
				case 1:
				case 4:
				case 8:
				case 16:
				case 20:
					for (i = 0; i<reg_dsize+1; i++)
						lbuf[i] = (unsigned char)param[i];

					i2c_master_send(this_client, lbuf, reg_dsize+1);
					rt5512_reg_priv[param[0]].sync_needed = 1;
					break;
				default:
					rc = -EINVAL;
					break;
			}
		}
		else
			rc = -EINVAL;
	} else if (!strcmp(access_str, "peek")) {
		/* read */
		rc = get_parameters(lbuf, param, 1);
		if (rc > 0 && param[0] <= RT5512_MAX_REG)
		{
			reg_dsize = rt5512_reg_priv[param[0]].reg_size;
			switch (reg_dsize)
			{
				case 1:
				case 4:
				case 8:
				case 16:
				case 20:
					lbuf[0] = (unsigned char)param[0];
					i2c_master_send(this_client, lbuf, 1);
					i2c_master_recv(this_client, lbuf, reg_dsize);
					for (i=0; i< reg_dsize; i++)
						read_data[i] = lbuf[i];
					read_size = reg_dsize;
					break;
				default:
					break;
			}
		}
		else
			rc = -EINVAL;
	} else if (!strcmp(access_str, "headset_test")) {
		rc = get_parameters(lbuf, param, 1);
		if (rc > 0)
		{
			if (param[0] >= 0 && param[0] <= 6)
				rt5512_simulate_headset(this_client, param[0]);
			else
				rc = -EINVAL;
		}
		else
			rc = -EINVAL;
	}

	if (rc > 0)
		rc = cnt;

	return rc;
}

static const struct file_operations codec_debug_ops = {
	.open = codec_debug_open,
	.write = codec_debug_write,
	.read = codec_debug_read
};
#endif

static int __devinit rt5512_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	struct rt5512_codec_chip *chip;

	RT_DBG("\n");
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
	{
		pr_err("%s: could not allocate kernel memory\n", __func__);
		return -ENOMEM;
	}

	chip->dev = &client->dev;
	chip->client = client;
	chip->control_type = SND_SOC_I2C;
	
	i2c_set_clientdata(client, chip);

	ret = snd_soc_register_codec(&client->dev,
			&soc_codec_dev_rt5512, rt5512_dai, ARRAY_SIZE(rt5512_dai));
	if (ret < 0)
	{
		dev_err(&client->dev, "register codec failed\n");
		goto codec_reg_fail;
	}

	#ifdef CONFIG_DEBUG_FS
	RT_DBG("add debugfs for core debug\n");
	this_client = client;
	debugfs_rt_dent = debugfs_create_dir("rt5512_codec_dbg", 0);
	if (!IS_ERR(debugfs_rt_dent)) {
		debugfs_peek = debugfs_create_file("peek",
		S_IFREG | S_IRUGO, debugfs_rt_dent,
		(void *) "peek", &codec_debug_ops);

		debugfs_poke = debugfs_create_file("poke",
		S_IFREG | S_IRUGO, debugfs_rt_dent,
		(void *) "poke", &codec_debug_ops);

		debugfs_headset_test = debugfs_create_file("headset_test",
		S_IFREG | S_IRUGO, debugfs_rt_dent,
		(void *) "headset_test", &codec_debug_ops);
	}
	#endif

	pr_info("rt5512-codec driver successfully loaded\n");
	return 0;

codec_reg_fail:
	kfree(chip);
	return ret;
}

static int __devexit rt5512_i2c_remove(struct i2c_client *client)
{
	struct rt5512_codec_chip *chip = i2c_get_clientdata(client);
	
	if (!IS_ERR(debugfs_rt_dent))
		debugfs_remove_recursive(debugfs_rt_dent);
	kfree(chip);
	return 0;
}

static const struct i2c_device_id rt5512_i2c_id[] = 
{
	{"rt5512", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, rt5512_i2c_id);

struct i2c_driver rt5512_i2c_driver = 
{
	.driver = {
		.name = "rt5512",
		.owner = THIS_MODULE,
	},
	.probe = rt5512_i2c_probe,
	.remove = __devexit_p(rt5512_i2c_remove),
	.id_table = rt5512_i2c_id,
};

static int __init rt5512_init(void)
{
	return i2c_add_driver(&rt5512_i2c_driver);
}

static void __exit rt5512_exit(void)
{
	i2c_del_driver(&rt5512_i2c_driver);
}

module_init(rt5512_init);
module_exit(rt5512_exit);

MODULE_AUTHOR("cy_huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("RT5512 audio codec for Rockchip board");
MODULE_LICENSE("GPL");
MODULE_VERSION(RT5512_DRV_VER);
