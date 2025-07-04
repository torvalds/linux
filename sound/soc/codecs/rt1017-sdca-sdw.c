// SPDX-License-Identifier: GPL-2.0-only
//
// rt1017-sdca-sdw.c -- rt1017 SDCA ALSA SoC amplifier audio driver
//
// Copyright(c) 2023 Realtek Semiconductor Corp.
//
//
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rt1017-sdca-sdw.h"

static bool rt1017_sdca_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2f55:
	case 0x3206:
	case 0xc000:
	case 0xc001:
	case 0xc022:
	case 0xc030:
	case 0xc104:
	case 0xc10b:
	case 0xc10c:
	case 0xc110:
	case 0xc112:
	case 0xc300:
	case 0xc301:
	case 0xc318:
	case 0xc325 ... 0xc328:
	case 0xc331:
	case 0xc340:
	case 0xc350 ... 0xc351:
	case 0xc500:
	case 0xc502:
	case 0xc504:
	case 0xc507:
	case 0xc509:
	case 0xc510:
	case 0xc512:
	case 0xc518:
	case 0xc51b:
	case 0xc51d:
	case 0xc520:
	case 0xc540 ... 0xc542:
	case 0xc550 ... 0xc552:
	case 0xc600:
	case 0xc602:
	case 0xc612:
	case 0xc622:
	case 0xc632:
	case 0xc642:
	case 0xc651:
	case 0xca00:
	case 0xca09 ... 0xca0c:
	case 0xca0e ... 0xca0f:
	case 0xca10 ... 0xca11:
	case 0xca16 ... 0xca17:
	case 0xcb00:
	case 0xcc00:
	case 0xcc02:
	case 0xd017:
	case 0xd01a ... 0xd01c:
	case 0xd101:
	case 0xd20c:
	case 0xd300:
	case 0xd370:
	case 0xd500:
	case 0xd545 ... 0xd548:
	case 0xd5a5 ... 0xd5a8:
	case 0xd5aa ... 0xd5ad:
	case 0xda04 ... 0xda07:
	case 0xda09 ... 0xda0a:
	case 0xda0c ... 0xda0f:
	case 0xda11 ... 0xda14:
	case 0xda16 ... 0xda19:
	case 0xdab6 ... 0xdabb:
	case 0xdb09 ... 0xdb0a:
	case 0xdb14:

	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_UDMPU21,
			RT1017_SDCA_CTL_UDMPU_CLUSTER, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_FU,
			RT1017_SDCA_CTL_FU_MUTE, 0x01):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_XU22,
			RT1017_SDCA_CTL_BYPASS, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_SAPU29,
			RT1017_SDCA_CTL_PROT_STAT, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_CS21,
			RT1017_SDCA_CTL_FS_INDEX, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_PDE23,
			RT1017_SDCA_CTL_REQ_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_PDE22,
			RT1017_SDCA_CTL_REQ_POWER_STATE, 0):
		return true;
	default:
		return false;
	}
}

static bool rt1017_sdca_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2f55:
	case 0xc000:
	case 0xc022:
	case 0xc351:
	case 0xc518:
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_SAPU29,
			RT1017_SDCA_CTL_PROT_STAT, 0):
		return true;
	default:
		return false;
	}
}

static const struct reg_sequence rt1017_blind_write[] = {
	{ 0xc001, 0x43 },
	{ 0x2f55, 0x02 },
	{ 0x3206, 0x80 },
	{ 0x005f, 0x7f },
	{ 0xd101, 0xa0 },
	{ 0xc112, 0xc0 },
	{ 0xc104, 0xaa },
	{ 0xc110, 0x59 },
	{ 0xc112, 0xc0 },
	{ 0xc340, 0x80 },
	{ 0xd017, 0x2c },
	{ 0xd01a, 0xc8 },
	{ 0xd01b, 0xcf },
	{ 0xd01c, 0x0c },
	{ 0xd20c, 0x14 },
	{ 0xdb09, 0x0f },
	{ 0xdb0a, 0x7f },
	{ 0xdb14, 0x03 },
	{ 0xcb00, 0x31 },
	{ 0xc318, 0x44 },
	{ 0xc325, 0xce },
	{ 0xc326, 0x13 },
	{ 0xc327, 0x5f },
	{ 0xc328, 0xf3 },
	{ 0xc350, 0xe1 },
	{ 0xc351, 0x88 },
	{ 0xc030, 0x14 },
	{ 0xc331, 0xf2 },
	{ 0xc551, 0x0f },
	{ 0xc552, 0xff },
	{ 0xc651, 0xc0 },
	{ 0xc550, 0xd0 },
	{ 0xc612, 0x00 },
	{ 0xc622, 0x00 },
	{ 0xc632, 0x00 },
	{ 0xc642, 0x00 },
	{ 0xc602, 0xf0 },
	{ 0xc600, 0xd0 },
	{ 0xcc02, 0x78 },
	{ 0xcc00, 0x90 },
	{ 0xc300, 0x3f },
	{ 0xc301, 0x1d },
	{ 0xc10b, 0x2e },
	{ 0xc10c, 0x36 },

	{ 0xd5a5, 0x00 },
	{ 0xd5a6, 0x6a },
	{ 0xd5a7, 0xaa },
	{ 0xd5a8, 0xaa },
	{ 0xd5aa, 0x00 },
	{ 0xd5ab, 0x16 },
	{ 0xd5ac, 0xdb },
	{ 0xd5ad, 0x6d },
	{ 0xd545, 0x09 },
	{ 0xd546, 0x30 },
	{ 0xd547, 0xf0 },
	{ 0xd548, 0xf0 },
	{ 0xd500, 0x20 },
	{ 0xc504, 0x3f },
	{ 0xc540, 0x00 },
	{ 0xc541, 0x0a },
	{ 0xc542, 0x1a },
	{ 0xc512, 0x00 },
	{ 0xc520, 0x40 },
	{ 0xc51b, 0x7f },
	{ 0xc51d, 0x0f },
	{ 0xc500, 0x40 },
	{ 0xc502, 0xde },
	{ 0xc507, 0x05 },
	{ 0xc509, 0x05 },
	{ 0xc510, 0x40 },
	{ 0xc518, 0xc0 },
	{ 0xc500, 0xc0 },

	{ 0xda0c, 0x00 },
	{ 0xda0d, 0x0b },
	{ 0xda0e, 0x55 },
	{ 0xda0f, 0x55 },
	{ 0xda04, 0x00 },
	{ 0xda05, 0x51 },
	{ 0xda06, 0xeb },
	{ 0xda07, 0x85 },
	{ 0xca16, 0x0f },
	{ 0xca17, 0x00 },
	{ 0xda09, 0x5d },
	{ 0xda0a, 0xc0 },
	{ 0xda11, 0x26 },
	{ 0xda12, 0x66 },
	{ 0xda13, 0x66 },
	{ 0xda14, 0x66 },
	{ 0xda16, 0x79 },
	{ 0xda17, 0x99 },
	{ 0xda18, 0x99 },
	{ 0xda19, 0x99 },
	{ 0xca09, 0x00 },
	{ 0xca0a, 0x07 },
	{ 0xca0b, 0x89 },
	{ 0xca0c, 0x61 },
	{ 0xca0e, 0x00 },
	{ 0xca0f, 0x03 },
	{ 0xca10, 0xc4 },
	{ 0xca11, 0xb0 },
	{ 0xdab6, 0x00 },
	{ 0xdab7, 0x01 },
	{ 0xdab8, 0x00 },
	{ 0xdab9, 0x00 },
	{ 0xdaba, 0x00 },
	{ 0xdabb, 0x00 },
	{ 0xd017, 0x0e },
	{ 0xca00, 0xcd },
	{ 0xc022, 0x84 },
};

#define RT1017_MAX_REG_NUM 0x4108ffff

static const struct regmap_config rt1017_sdca_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = rt1017_sdca_readable_register,
	.volatile_reg = rt1017_sdca_volatile_register,
	.max_register = RT1017_MAX_REG_NUM,
	.reg_defaults = rt1017_sdca_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt1017_sdca_reg_defaults),
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt1017_sdca_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval;
	int i, j;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;

	prop->paging_support = true;

	/* first we need to allocate memory for set bits in port lists
	 * port = 1 for AMP playback
	 * port = 2 for IV capture
	 */
	prop->source_ports = BIT(2); /* BITMAP: 00000100 */
	prop->sink_ports = BIT(1);   /* BITMAP: 00000010 */

	nval = hweight32(prop->source_ports);
	prop->src_dpn_prop = devm_kcalloc(&slave->dev, nval,
		sizeof(*prop->src_dpn_prop), GFP_KERNEL);
	if (!prop->src_dpn_prop)
		return -ENOMEM;

	i = 0;
	dpn = prop->src_dpn_prop;
	addr = prop->source_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[i].num = bit;
		dpn[i].type = SDW_DPN_FULL;
		dpn[i].simple_ch_prep_sm = true;
		dpn[i].ch_prep_timeout = 10;
		i++;
	}

	/* do this again for sink now */
	nval = hweight32(prop->sink_ports);
	prop->sink_dpn_prop = devm_kcalloc(&slave->dev, nval,
		sizeof(*prop->sink_dpn_prop), GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;

	j = 0;
	dpn = prop->sink_dpn_prop;
	addr = prop->sink_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[j].num = bit;
		dpn[j].type = SDW_DPN_FULL;
		dpn[j].simple_ch_prep_sm = true;
		dpn[j].ch_prep_timeout = 10;
		j++;
	}

	/* set the timeout values */
	prop->clk_stop_timeout = 64;

	return 0;
}

static int rt1017_sdca_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt1017_sdca_priv *rt1017 = dev_get_drvdata(dev);

	if (rt1017->hw_init)
		return 0;

	if (rt1017->first_hw_init) {
		regcache_cache_only(rt1017->regmap, false);
		regcache_cache_bypass(rt1017->regmap, true);
	} else {
		/*
		 * PM runtime is only enabled when a Slave reports as Attached
		 */

		/* set autosuspend parameters */
		pm_runtime_set_autosuspend_delay(&slave->dev, 3000);
		pm_runtime_use_autosuspend(&slave->dev);

		/* update count of parent 'active' children */
		pm_runtime_set_active(&slave->dev);

		/* make sure the device does not suspend immediately */
		pm_runtime_mark_last_busy(&slave->dev);

		pm_runtime_enable(&slave->dev);
	}

	pm_runtime_get_noresume(&slave->dev);

	/* sw reset */
	regmap_write(rt1017->regmap, 0xc000, 0x02);

	/* initial settings - blind write */
	regmap_multi_reg_write(rt1017->regmap, rt1017_blind_write,
		ARRAY_SIZE(rt1017_blind_write));

	if (rt1017->first_hw_init) {
		regcache_cache_bypass(rt1017->regmap, false);
		regcache_mark_dirty(rt1017->regmap);
	} else
		rt1017->first_hw_init = true;

	/* Mark Slave initialization complete */
	rt1017->hw_init = true;

	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "hw_init complete\n");
	return 0;
}

static int rt1017_sdca_update_status(struct sdw_slave *slave,
				enum sdw_slave_status status)
{
	struct  rt1017_sdca_priv *rt1017 = dev_get_drvdata(&slave->dev);

	if (status == SDW_SLAVE_UNATTACHED)
		rt1017->hw_init = false;

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt1017->hw_init || status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt1017_sdca_io_init(&slave->dev, slave);
}

static const char * const rt1017_rx_data_ch_select[] = {
	"Bypass",
	"CN1",
	"CN2",
	"CN3",
	"CN4",
	"(1+2)/2",
	"(1+3)/2",
	"(1+4)/2",
	"(2+3)/2",
	"(2+4)/2",
	"(3+4)/2",
};

static SOC_ENUM_SINGLE_DECL(rt1017_rx_data_ch_enum,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_UDMPU21,
				RT1017_SDCA_CTL_UDMPU_CLUSTER, 0),
			0, rt1017_rx_data_ch_select);

static const struct snd_kcontrol_new rt1017_sdca_controls[] = {
	/* UDMPU Cluster Selection */
	SOC_ENUM("RX Channel Select", rt1017_rx_data_ch_enum),
};

static const struct snd_kcontrol_new rt1017_sto_dac =
	SOC_DAPM_SINGLE("Switch",
		SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_FU, RT1017_SDCA_CTL_FU_MUTE, 0x1),
		0, 1, 1);

static int rt1017_sdca_pde23_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt1017_sdca_priv *rt1017 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt1017->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_PDE23,
				RT1017_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt1017->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_PDE23,
				RT1017_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;
	default:
		break;
	}
	return 0;
}

static int rt1017_sdca_classd_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt1017_sdca_priv *rt1017 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(rt1017->regmap, RT1017_PWM_TRIM_1,
			RT1017_PWM_FREQ_CTL_SRC_SEL_MASK, RT1017_PWM_FREQ_CTL_SRC_SEL_REG);
		regmap_write(rt1017->regmap, RT1017_CLASSD_INT_1, 0x10);
		break;
	default:
		break;
	}

	return 0;
}

static int rt1017_sdca_feedback_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt1017_sdca_priv *rt1017 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(rt1017->regmap, 0xd017, 0x1f, 0x08);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(rt1017->regmap, 0xd017, 0x1f, 0x09);
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt1017_sdca_dapm_widgets[] = {
	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("DP1RX", "DP1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT_E("DP2TX", "DP2 Capture", 0, SND_SOC_NOPM, 0, 0,
		rt1017_sdca_feedback_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* Digital Interface */
	SND_SOC_DAPM_SWITCH("DAC", SND_SOC_NOPM, 0, 0, &rt1017_sto_dac),

	/* Output Lines */
	SND_SOC_DAPM_PGA_E("CLASS D", SND_SOC_NOPM, 0, 0, NULL, 0,
		rt1017_sdca_classd_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("SPO"),

	SND_SOC_DAPM_SUPPLY("PDE23", SND_SOC_NOPM, 0, 0,
		rt1017_sdca_pde23_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_PGA("I Sense", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("V Sense", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SIGGEN("I Gen"),
	SND_SOC_DAPM_SIGGEN("V Gen"),
};

static const struct snd_soc_dapm_route rt1017_sdca_dapm_routes[] = {

	{ "DAC", "Switch", "DP1RX" },
	{ "CLASS D", NULL, "DAC" },
	{ "CLASS D", NULL, "PDE23" },
	{ "SPO", NULL, "CLASS D" },

	{ "I Sense", NULL, "I Gen" },
	{ "V Sense", NULL, "V Gen" },
	{ "I Sense", NULL, "PDE23" },
	{ "V Sense", NULL, "PDE23" },
	{ "DP2TX", NULL, "I Sense" },
	{ "DP2TX", NULL, "V Sense" },
};

static const struct sdw_slave_ops rt1017_sdca_slave_ops = {
	.read_prop = rt1017_sdca_read_prop,
	.update_status = rt1017_sdca_update_status,
};

static int rt1017_sdca_component_probe(struct snd_soc_component *component)
{
	int ret;

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	return 0;
}

static void rt1017_sdca_component_remove(struct snd_soc_component *component)
{
	struct rt1017_sdca_priv *rt1017 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1017->regmap, true);
}

static const struct snd_soc_component_driver soc_sdca_component_rt1017 = {
	.probe = rt1017_sdca_component_probe,
	.remove = rt1017_sdca_component_remove,
	.controls = rt1017_sdca_controls,
	.num_controls = ARRAY_SIZE(rt1017_sdca_controls),
	.dapm_widgets = rt1017_sdca_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt1017_sdca_dapm_widgets),
	.dapm_routes = rt1017_sdca_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt1017_sdca_dapm_routes),
	.endianness = 1,
};

static int rt1017_sdca_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void rt1017_sdca_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int rt1017_sdca_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1017_sdca_priv *rt1017 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config;
	struct sdw_port_config port_config;
	enum sdw_data_direction direction;
	struct sdw_stream_runtime *sdw_stream;
	int retval, port, num_channels, ch_mask;
	unsigned int sampling_rate;

	dev_dbg(dai->dev, "%s %s", __func__, dai->name);
	sdw_stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!sdw_stream)
		return -EINVAL;

	if (!rt1017->sdw_slave)
		return -EINVAL;

	/* SoundWire specific configuration */
	/* port 1 for playback */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		direction = SDW_DATA_DIR_RX;
		port = 1;
	} else {
		direction = SDW_DATA_DIR_TX;
		port = 2;
	}

	num_channels = params_channels(params);
	ch_mask = (1 << num_channels) - 1;

	stream_config.frame_rate = params_rate(params);
	stream_config.ch_count = num_channels;
	stream_config.bps = snd_pcm_format_width(params_format(params));
	stream_config.direction = direction;

	port_config.ch_mask = ch_mask;
	port_config.num = port;

	dev_dbg(dai->dev, "frame_rate %d, ch_count %d, bps %d, direction %d, ch_mask %d, port: %d\n",
		params_rate(params), num_channels, snd_pcm_format_width(params_format(params)),
		direction, ch_mask, port);

	retval = sdw_stream_add_slave(rt1017->sdw_slave, &stream_config,
				&port_config, 1, sdw_stream);
	if (retval) {
		dev_err(dai->dev, "Unable to configure port\n");
		return retval;
	}

	/* sampling rate configuration */
	switch (params_rate(params)) {
	case 44100:
		sampling_rate = RT1017_SDCA_RATE_44100HZ;
		break;
	case 48000:
		sampling_rate = RT1017_SDCA_RATE_48000HZ;
		break;
	case 96000:
		sampling_rate = RT1017_SDCA_RATE_96000HZ;
		break;
	case 192000:
		sampling_rate = RT1017_SDCA_RATE_192000HZ;
		break;
	default:
		dev_err(component->dev, "Rate %d is not supported\n",
			params_rate(params));
		return -EINVAL;
	}

	/* set sampling frequency */
	regmap_write(rt1017->regmap,
		SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1017_SDCA_ENT_CS21,
			RT1017_SDCA_CTL_FS_INDEX, 0),
		sampling_rate);

	return 0;
}

static int rt1017_sdca_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1017_sdca_priv *rt1017 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *sdw_stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt1017->sdw_slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt1017->sdw_slave, sdw_stream);
	return 0;
}

static const struct snd_soc_dai_ops rt1017_sdca_ops = {
	.hw_params	= rt1017_sdca_pcm_hw_params,
	.hw_free	= rt1017_sdca_pcm_hw_free,
	.set_stream	= rt1017_sdca_set_sdw_stream,
	.shutdown	= rt1017_sdca_shutdown,
};

#define RT1017_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
			     SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define RT1017_FORMATS (SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver rt1017_sdca_dai[] = {
	{
		.name = "rt1017-aif",
		.playback = {
			.stream_name = "DP1 Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = RT1017_STEREO_RATES,
			.formats = RT1017_FORMATS,
		},
		.capture = {
			.stream_name = "DP2 Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = RT1017_STEREO_RATES,
			.formats = RT1017_FORMATS,
		},
		.ops = &rt1017_sdca_ops,
	},
};

static int rt1017_sdca_init(struct device *dev, struct regmap *regmap,
			struct sdw_slave *slave)
{
	struct rt1017_sdca_priv *rt1017;
	int ret;

	rt1017 = devm_kzalloc(dev, sizeof(*rt1017), GFP_KERNEL);
	if (!rt1017)
		return -ENOMEM;

	dev_set_drvdata(dev, rt1017);
	rt1017->sdw_slave = slave;
	rt1017->regmap = regmap;

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt1017->hw_init = false;
	rt1017->first_hw_init = false;

	ret =  devm_snd_soc_register_component(dev,
				&soc_sdca_component_rt1017,
				rt1017_sdca_dai,
				ARRAY_SIZE(rt1017_sdca_dai));

	return ret;
}

static int rt1017_sdca_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct regmap *regmap;

	/* Regmap Initialization */
	regmap = devm_regmap_init_sdw(slave, &rt1017_sdca_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return rt1017_sdca_init(&slave->dev, regmap, slave);
}

static int rt1017_sdca_sdw_remove(struct sdw_slave *slave)
{
	struct rt1017_sdca_priv *rt1017 = dev_get_drvdata(&slave->dev);

	if (rt1017->first_hw_init)
		pm_runtime_disable(&slave->dev);

	return 0;
}

static const struct sdw_device_id rt1017_sdca_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x1017, 0x3, 0x1, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt1017_sdca_id);

static int rt1017_sdca_dev_suspend(struct device *dev)
{
	struct rt1017_sdca_priv *rt1017 = dev_get_drvdata(dev);

	if (!rt1017->hw_init)
		return 0;

	regcache_cache_only(rt1017->regmap, true);

	return 0;
}

#define RT1017_PROBE_TIMEOUT 5000

static int rt1017_sdca_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt1017_sdca_priv *rt1017 = dev_get_drvdata(dev);
	unsigned long time;

	if (!rt1017->first_hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->initialization_complete,
				msecs_to_jiffies(RT1017_PROBE_TIMEOUT));
	if (!time) {
		dev_err(&slave->dev, "Initialization not complete, timed out\n");
		sdw_show_ping_status(slave->bus, true);

		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(rt1017->regmap, false);
	regcache_sync(rt1017->regmap);

	return 0;
}

static const struct dev_pm_ops rt1017_sdca_pm = {
	SYSTEM_SLEEP_PM_OPS(rt1017_sdca_dev_suspend, rt1017_sdca_dev_resume)
	RUNTIME_PM_OPS(rt1017_sdca_dev_suspend, rt1017_sdca_dev_resume, NULL)
};

static struct sdw_driver rt1017_sdca_sdw_driver = {
	.driver = {
		.name = "rt1017-sdca",
		.pm = pm_ptr(&rt1017_sdca_pm),
	},
	.probe = rt1017_sdca_sdw_probe,
	.remove = rt1017_sdca_sdw_remove,
	.ops = &rt1017_sdca_slave_ops,
	.id_table = rt1017_sdca_id,
};
module_sdw_driver(rt1017_sdca_sdw_driver);

MODULE_DESCRIPTION("ASoC RT1017 driver SDCA SDW");
MODULE_AUTHOR("Derek Fang <derek.fang@realtek.com>");
MODULE_LICENSE("GPL");
