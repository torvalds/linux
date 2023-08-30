// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022, Analog Devices Inc.

#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "max98363.h"

static struct reg_default max98363_reg[] = {
	{MAX98363_R2021_ERR_MON_CTRL, 0x0},
	{MAX98363_R2022_SPK_MON_THRESH, 0x0},
	{MAX98363_R2023_SPK_MON_DURATION, 0x0},
	{MAX98363_R2030_TONE_GEN_CFG, 0x0},
	{MAX98363_R203F_TONE_GEN_EN, 0x0},
	{MAX98363_R2040_AMP_VOL, 0x0},
	{MAX98363_R2041_AMP_GAIN, 0x5},
	{MAX98363_R2042_DSP_CFG, 0x0},
};

static bool max98363_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98363_R2001_INTR_RAW:
	case MAX98363_R2003_INTR_STATE:
	case MAX98363_R2005_INTR_FALG:
	case MAX98363_R2007_INTR_EN:
	case MAX98363_R2009_INTR_CLR:
	case MAX98363_R2021_ERR_MON_CTRL ... MAX98363_R2023_SPK_MON_DURATION:
	case MAX98363_R2030_TONE_GEN_CFG:
	case MAX98363_R203F_TONE_GEN_EN:
	case MAX98363_R2040_AMP_VOL:
	case MAX98363_R2041_AMP_GAIN:
	case MAX98363_R2042_DSP_CFG:
	case MAX98363_R21FF_REV_ID:
		return true;
	default:
		return false;
	}
};

static bool max98363_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98363_R2001_INTR_RAW:
	case MAX98363_R2003_INTR_STATE:
	case MAX98363_R2005_INTR_FALG:
	case MAX98363_R2007_INTR_EN:
	case MAX98363_R2009_INTR_CLR:
	case MAX98363_R21FF_REV_ID:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config max98363_sdw_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.max_register = MAX98363_R21FF_REV_ID,
	.reg_defaults  = max98363_reg,
	.num_reg_defaults = ARRAY_SIZE(max98363_reg),
	.readable_reg = max98363_readable_register,
	.volatile_reg = max98363_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static int max98363_suspend(struct device *dev)
{
	struct max98363_priv *max98363 = dev_get_drvdata(dev);

	regcache_cache_only(max98363->regmap, true);
	regcache_mark_dirty(max98363->regmap);

	return 0;
}

#define MAX98363_PROBE_TIMEOUT 5000

static int max98363_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct max98363_priv *max98363 = dev_get_drvdata(dev);
	unsigned long time;

	if (!max98363->first_hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->initialization_complete,
					   msecs_to_jiffies(MAX98363_PROBE_TIMEOUT));
	if (!time) {
		dev_err(dev, "Initialization not complete, timed out\n");
		return -ETIMEDOUT;
	}

regmap_sync:

	slave->unattach_request = 0;
	regcache_cache_only(max98363->regmap, false);
	regcache_sync(max98363->regmap);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(max98363_pm, max98363_suspend, max98363_resume, NULL);

static int max98363_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval, i;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;

	/* BITMAP: 00000010  Dataport 1 is active */
	prop->sink_ports = BIT(1);
	prop->paging_support = true;
	prop->clk_stop_timeout = 20;
	prop->simple_clk_stop_capable = true;
	prop->clock_reg_supported = true;

	nval = hweight32(prop->sink_ports);
	prop->sink_dpn_prop = devm_kcalloc(&slave->dev, nval,
					   sizeof(*prop->sink_dpn_prop),
					   GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;

	i = 0;
	dpn = prop->sink_dpn_prop;
	addr = prop->sink_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[i].num = bit;
		dpn[i].type = SDW_DPN_FULL;
		dpn[i].simple_ch_prep_sm = true;
		dpn[i].ch_prep_timeout = 10;
		i++;
	}

	return 0;
}

static int max98363_io_init(struct sdw_slave *slave)
{
	struct device *dev = &slave->dev;
	struct max98363_priv *max98363 = dev_get_drvdata(dev);
	int ret, reg;

	regcache_cache_only(max98363->regmap, false);
	if (max98363->first_hw_init)
		regcache_cache_bypass(max98363->regmap, true);

	/*
	 * PM runtime status is marked as 'active' only when a Slave reports as Attached
	 */
	if (!max98363->first_hw_init)
		/* update count of parent 'active' children */
		pm_runtime_set_active(dev);

	pm_runtime_get_noresume(dev);

	ret = regmap_read(max98363->regmap, MAX98363_R21FF_REV_ID, &reg);
	if (!ret)
		dev_info(dev, "Revision ID: %X\n", reg);
	else
		goto out;

	if (max98363->first_hw_init) {
		regcache_cache_bypass(max98363->regmap, false);
		regcache_mark_dirty(max98363->regmap);
	}

	max98363->first_hw_init = true;
	max98363->hw_init = true;

out:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

#define MAX98363_RATES SNDRV_PCM_RATE_8000_192000
#define MAX98363_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static int max98363_sdw_dai_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct max98363_priv *max98363 =
		snd_soc_component_get_drvdata(component);

	struct sdw_stream_config stream_config;
	struct sdw_port_config port_config;
	enum sdw_data_direction direction;
	struct sdw_stream_runtime *stream;
	struct snd_pcm_runtime *runtime = substream->runtime;

	int ret;

	stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!stream)
		return -EINVAL;

	if (!max98363->slave)
		return -EINVAL;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return -EINVAL;

	direction = SDW_DATA_DIR_RX;
	port_config.num = 1;

	stream_config.frame_rate = params_rate(params);
	stream_config.bps = snd_pcm_format_width(params_format(params));
	stream_config.direction = direction;
	stream_config.ch_count = 1;

	if (stream_config.ch_count > runtime->hw.channels_max) {
		stream_config.ch_count = runtime->hw.channels_max;
		dev_info(dai->dev, "Number of channels: %d (requested: %d)\n",
			 stream_config.ch_count, params_channels(params));
	}
	port_config.ch_mask = GENMASK((int)stream_config.ch_count - 1, 0);

	ret = sdw_stream_add_slave(max98363->slave, &stream_config,
				   &port_config, 1, stream);
	if (ret) {
		dev_err(dai->dev, "Unable to configure port\n");
		return ret;
	}

	dev_dbg(component->dev, "Format supported %d", params_format(params));

	return 0;
}

static int max98363_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct max98363_priv *max98363 =
		snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!max98363->slave)
		return -EINVAL;

	sdw_stream_remove_slave(max98363->slave, stream);

	return 0;
}

static int max98363_set_sdw_stream(struct snd_soc_dai *dai,
				   void *sdw_stream, int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static const struct snd_soc_dai_ops max98363_dai_sdw_ops = {
	.hw_params = max98363_sdw_dai_hw_params,
	.hw_free = max98363_pcm_hw_free,
	.set_stream = max98363_set_sdw_stream,
};

static struct snd_soc_dai_driver max98363_dai[] = {
	{
		.name = "max98363-aif1",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = MAX98363_RATES,
			.formats = MAX98363_FORMATS,
		},
		.ops = &max98363_dai_sdw_ops,
	}
};

static int max98363_update_status(struct sdw_slave *slave,
				  enum sdw_slave_status status)
{
	struct max98363_priv *max98363 = dev_get_drvdata(&slave->dev);

	if (status == SDW_SLAVE_UNATTACHED)
		max98363->hw_init = false;

	/*
	 * Perform initialization only if slave status is SDW_SLAVE_ATTACHED
	 */
	if (max98363->hw_init || status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return max98363_io_init(slave);
}

static struct sdw_slave_ops max98363_slave_ops = {
	.read_prop = max98363_read_prop,
	.update_status = max98363_update_status,
};

static DECLARE_TLV_DB_SCALE(max98363_digital_tlv, -6350, 50, 1);
static const DECLARE_TLV_DB_RANGE(max98363_spk_tlv,
	0, 5, TLV_DB_SCALE_ITEM(-300, 300, 0),
);

static const char * const max98363_tone_cfg_text[] = {
	"Reserved", "0", "+FS/2", "-FS/2", "1KHz",
	"12KHz", "8KHz", "6KHz", "4KHz", "3KHz",
	"2KHz",	"1.5KHz", "Reserved", "500Hz", "250Hz"
};

static SOC_ENUM_SINGLE_DECL(max98363_tone_cfg_enum,
			    MAX98363_R2030_TONE_GEN_CFG, 0,
			    max98363_tone_cfg_text);

static const char * const max98363_spkmon_duration_text[] = {
	"8ms", "20ms", "40ms", "60ms",
	"80ms", "160ms", "240ms", "320ms",
	"400ms", "480ms", "560ms", "640ms",
	"720ms", "800ms", "880ms", "960ms"
};

static SOC_ENUM_SINGLE_DECL(max98363_spkmon_duration_enum,
			    MAX98363_R2023_SPK_MON_DURATION, 0,
			    max98363_spkmon_duration_text);

static const struct snd_kcontrol_new max98363_snd_controls[] = {
	SOC_SINGLE_TLV("Digital Volume", MAX98363_R2040_AMP_VOL,
		       0, 0x7F, 1, max98363_digital_tlv),
	SOC_SINGLE_TLV("Speaker Volume", MAX98363_R2041_AMP_GAIN,
		       0, 10, 0, max98363_spk_tlv),
	SOC_SINGLE("Tone Generator Switch", MAX98363_R203F_TONE_GEN_EN,
		   0, 1, 0),
	SOC_ENUM("Tone Config", max98363_tone_cfg_enum),
	SOC_SINGLE("Ramp Switch", MAX98363_R2042_DSP_CFG,
		   MAX98363_AMP_DSP_CFG_RMP_SHIFT, 1, 0),
	SOC_SINGLE("CLK Monitor Switch", MAX98363_R2021_ERR_MON_CTRL,
		   MAX98363_CLOCK_MON_SHIFT, 1, 0),
	SOC_SINGLE("SPKMON Monitor Switch", MAX98363_R2021_ERR_MON_CTRL,
		   MAX98363_SPKMON_SHIFT, 1, 0),
	SOC_SINGLE("SPKMON Thresh", MAX98363_R2022_SPK_MON_THRESH, 0, 0xFF, 0),
	SOC_ENUM("SPKMON Duration", max98363_spkmon_duration_enum),
};

static const struct snd_soc_dapm_widget max98363_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("AIFIN", "HiFi Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("BE_OUT"),
};

static const struct snd_soc_dapm_route max98363_audio_map[] = {
	/* Plabyack */
	{"BE_OUT", NULL, "AIFIN"},
};

static const struct snd_soc_component_driver soc_codec_dev_max98363 = {
	.controls		= max98363_snd_controls,
	.num_controls		= ARRAY_SIZE(max98363_snd_controls),
	.dapm_widgets		= max98363_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98363_dapm_widgets),
	.dapm_routes		= max98363_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(max98363_audio_map),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int max98363_init(struct sdw_slave *slave, struct regmap *regmap)
{
	struct max98363_priv *max98363;
	int ret;
	struct device *dev = &slave->dev;

	/*  Allocate and assign private driver data structure  */
	max98363 = devm_kzalloc(dev, sizeof(*max98363), GFP_KERNEL);
	if (!max98363)
		return -ENOMEM;

	dev_set_drvdata(dev, max98363);
	max98363->regmap = regmap;
	max98363->slave = slave;

	regcache_cache_only(max98363->regmap, true);

	max98363->hw_init = false;
	max98363->first_hw_init = false;

	/* codec registration  */
	ret = devm_snd_soc_register_component(dev, &soc_codec_dev_max98363,
					      max98363_dai,
					      ARRAY_SIZE(max98363_dai));
	if (ret < 0) {
		dev_err(dev, "Failed to register codec: %d\n", ret);
		return ret;
	}

	/* set autosuspend parameters */
	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);

	/* make sure the device does not suspend immediately */
	pm_runtime_mark_last_busy(dev);

	pm_runtime_enable(dev);

	/* important note: the device is NOT tagged as 'active' and will remain
	 * 'suspended' until the hardware is enumerated/initialized. This is required
	 * to make sure the ASoC framework use of pm_runtime_get_sync() does not silently
	 * fail with -EACCESS because of race conditions between card creation and enumeration
	 */
	return 0;
}

static int max98363_sdw_probe(struct sdw_slave *slave,
			      const struct sdw_device_id *id)
{
	struct regmap *regmap;

	/* Regmap Initialization */
	regmap = devm_regmap_init_sdw(slave, &max98363_sdw_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return max98363_init(slave, regmap);
}

static const struct sdw_device_id max98363_id[] = {
	SDW_SLAVE_ENTRY(0x019F, 0x8363, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, max98363_id);

static struct sdw_driver max98363_sdw_driver = {
	.driver = {
		.name = "max98363",
		.pm = pm_ptr(&max98363_pm),
	},
	.probe = max98363_sdw_probe,
	.ops = &max98363_slave_ops,
	.id_table = max98363_id,
};

module_sdw_driver(max98363_sdw_driver);

MODULE_DESCRIPTION("ASoC MAX98363 driver SDW");
MODULE_AUTHOR("Ryan Lee <ryans.lee@analog.com>");
MODULE_LICENSE("GPL");
