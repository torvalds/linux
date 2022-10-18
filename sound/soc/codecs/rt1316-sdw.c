// SPDX-License-Identifier: GPL-2.0-only
//
// rt1316-sdw.c -- rt1316 SDCA ALSA SoC amplifier audio driver
//
// Copyright(c) 2021 Realtek Semiconductor Corp.
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
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include "rt1316-sdw.h"

static const struct reg_default rt1316_reg_defaults[] = {
	{ 0x3004, 0x00 },
	{ 0x3005, 0x00 },
	{ 0x3206, 0x00 },
	{ 0xc001, 0x00 },
	{ 0xc002, 0x00 },
	{ 0xc003, 0x00 },
	{ 0xc004, 0x00 },
	{ 0xc005, 0x00 },
	{ 0xc006, 0x00 },
	{ 0xc007, 0x00 },
	{ 0xc008, 0x00 },
	{ 0xc009, 0x00 },
	{ 0xc00a, 0x00 },
	{ 0xc00b, 0x00 },
	{ 0xc00c, 0x00 },
	{ 0xc00d, 0x00 },
	{ 0xc00e, 0x00 },
	{ 0xc00f, 0x00 },
	{ 0xc010, 0xa5 },
	{ 0xc011, 0x00 },
	{ 0xc012, 0xff },
	{ 0xc013, 0xff },
	{ 0xc014, 0x40 },
	{ 0xc015, 0x00 },
	{ 0xc016, 0x00 },
	{ 0xc017, 0x00 },
	{ 0xc605, 0x30 },
	{ 0xc700, 0x0a },
	{ 0xc701, 0xaa },
	{ 0xc702, 0x1a },
	{ 0xc703, 0x0a },
	{ 0xc710, 0x80 },
	{ 0xc711, 0x00 },
	{ 0xc712, 0x3e },
	{ 0xc713, 0x80 },
	{ 0xc714, 0x80 },
	{ 0xc715, 0x06 },
	{ 0xd101, 0x00 },
	{ 0xd102, 0x30 },
	{ 0xd103, 0x00 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_UDMPU21, RT1316_SDCA_CTL_UDMPU_CLUSTER, 0), 0x00 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_FU21, RT1316_SDCA_CTL_FU_MUTE, CH_L), 0x01 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_FU21, RT1316_SDCA_CTL_FU_MUTE, CH_R), 0x01 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_XU24, RT1316_SDCA_CTL_BYPASS, 0), 0x01 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE23, RT1316_SDCA_CTL_REQ_POWER_STATE, 0), 0x03 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE22, RT1316_SDCA_CTL_REQ_POWER_STATE, 0), 0x03 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE24, RT1316_SDCA_CTL_REQ_POWER_STATE, 0), 0x03 },
};

static const struct reg_sequence rt1316_blind_write[] = {
	{ 0xc710, 0x17 },
	{ 0xc711, 0x80 },
	{ 0xc712, 0x26 },
	{ 0xc713, 0x06 },
	{ 0xc714, 0x80 },
	{ 0xc715, 0x06 },
	{ 0xc702, 0x0a },
	{ 0xc703, 0x0a },
	{ 0xc001, 0x45 },
	{ 0xc003, 0x00 },
	{ 0xc004, 0x11 },
	{ 0xc005, 0x00 },
	{ 0xc006, 0x00 },
	{ 0xc106, 0x00 },
	{ 0xc007, 0x11 },
	{ 0xc008, 0x11 },
	{ 0xc009, 0x00 },

	{ 0x2f0a, 0x00 },
	{ 0xd101, 0xf0 },
	{ 0xd103, 0x9b },
	{ 0x2f36, 0x8e },
	{ 0x3206, 0x80 },
	{ 0x3211, 0x0b },
	{ 0x3216, 0x06 },
	{ 0xc614, 0x20 },
	{ 0xc615, 0x0a },
	{ 0xc616, 0x02 },
	{ 0xc617, 0x00 },
	{ 0xc60b, 0x10 },
	{ 0xc60e, 0x05 },
	{ 0xc102, 0x00 },
	{ 0xc090, 0xb0 },
	{ 0xc00f, 0x01 },
	{ 0xc09c, 0x7b },

	{ 0xc602, 0x07 },
	{ 0xc603, 0x07 },
	{ 0xc0a3, 0x71 },
	{ 0xc00b, 0x30 },
	{ 0xc093, 0x80 },
	{ 0xc09d, 0x80 },
	{ 0xc0b0, 0x77 },
	{ 0xc010, 0xa5 },
	{ 0xc050, 0x83 },
	{ 0x2f55, 0x03 },
	{ 0x3217, 0xb5 },
	{ 0x3202, 0x02 },

	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_XU24, RT1316_SDCA_CTL_BYPASS, 0), 0x00 },

	/* for IV sense */
	{ 0x2232, 0x80 },
	{ 0xc0b0, 0x77 },
	{ 0xc011, 0x00 },
	{ 0xc020, 0x00 },
	{ 0xc023, 0x00 },
	{ 0x3101, 0x00 },
	{ 0x3004, 0xa0 },
	{ 0x3005, 0xb1 },
	{ 0xc007, 0x11 },
	{ 0xc008, 0x11 },
	{ 0xc009, 0x00 },
	{ 0xc022, 0xd6 },
	{ 0xc025, 0xd6 },

	{ 0xd001, 0x03 },
	{ 0xd002, 0xbf },
	{ 0xd003, 0x03 },
	{ 0xd004, 0xbf },
};

static bool rt1316_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2f0a:
	case 0x2f36:
	case 0x3203 ... 0x320e:
	case 0xc000 ... 0xc7b4:
	case 0xcf00 ... 0xcf03:
	case 0xd101 ... 0xd103:
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_UDMPU21, RT1316_SDCA_CTL_UDMPU_CLUSTER, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_FU21, RT1316_SDCA_CTL_FU_MUTE, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_FU21, RT1316_SDCA_CTL_FU_MUTE, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE23, RT1316_SDCA_CTL_REQ_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE27, RT1316_SDCA_CTL_REQ_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE22, RT1316_SDCA_CTL_REQ_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE24, RT1316_SDCA_CTL_REQ_POWER_STATE, 0):
		return true;
	default:
		return false;
	}
}

static bool rt1316_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0xc000:
	case 0xc093:
	case 0xc09d:
	case 0xc0a3:
	case 0xc201:
	case 0xc427 ... 0xc428:
	case 0xd102:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rt1316_sdw_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = rt1316_readable_register,
	.volatile_reg = rt1316_volatile_register,
	.max_register = 0x4108ffff,
	.reg_defaults = rt1316_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt1316_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt1316_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval;
	int i, j;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;
	prop->is_sdca = true;

	prop->paging_support = true;

	/* first we need to allocate memory for set bits in port lists */
	prop->source_ports = 0x04; /* BITMAP: 00000100 */
	prop->sink_ports = 0x2; /* BITMAP:  00000010 */

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
	prop->clk_stop_timeout = 20;

	dev_dbg(&slave->dev, "%s\n", __func__);

	return 0;
}

static int rt1316_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt1316_sdw_priv *rt1316 = dev_get_drvdata(dev);

	if (rt1316->hw_init)
		return 0;

	if (rt1316->first_hw_init) {
		regcache_cache_only(rt1316->regmap, false);
		regcache_cache_bypass(rt1316->regmap, true);
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
	regmap_write(rt1316->regmap, 0xc000, 0x02);

	/* initial settings - blind write */
	regmap_multi_reg_write(rt1316->regmap, rt1316_blind_write,
		ARRAY_SIZE(rt1316_blind_write));

	if (rt1316->first_hw_init) {
		regcache_cache_bypass(rt1316->regmap, false);
		regcache_mark_dirty(rt1316->regmap);
	} else
		rt1316->first_hw_init = true;

	/* Mark Slave initialization complete */
	rt1316->hw_init = true;

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "%s hw_init complete\n", __func__);
	return 0;
}

static int rt1316_update_status(struct sdw_slave *slave,
					enum sdw_slave_status status)
{
	struct  rt1316_sdw_priv *rt1316 = dev_get_drvdata(&slave->dev);

	/* Update the status */
	rt1316->status = status;

	if (status == SDW_SLAVE_UNATTACHED)
		rt1316->hw_init = false;

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt1316->hw_init || rt1316->status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt1316_io_init(&slave->dev, slave);
}

static int rt1316_classd_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt1316_sdw_priv *rt1316 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt1316->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE23,
				RT1316_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		regmap_write(rt1316->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE27,
				RT1316_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		regmap_write(rt1316->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE22,
				RT1316_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt1316->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE23,
				RT1316_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		regmap_write(rt1316->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE27,
				RT1316_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		regmap_write(rt1316->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE22,
				RT1316_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;

	default:
		break;
	}

	return 0;
}

static int rt1316_pde24_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt1316_sdw_priv *rt1316 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt1316->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE24,
				RT1316_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt1316->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_PDE24,
				RT1316_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;
	}
	return 0;
}

static const char * const rt1316_rx_data_ch_select[] = {
	"L,R",
	"L,L",
	"L,R",
	"L,L+R",
	"R,L",
	"R,R",
	"R,L+R",
	"L+R,L",
	"L+R,R",
	"L+R,L+R",
};

static SOC_ENUM_SINGLE_DECL(rt1316_rx_data_ch_enum,
	SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_UDMPU21, RT1316_SDCA_CTL_UDMPU_CLUSTER, 0), 0,
	rt1316_rx_data_ch_select);

static const struct snd_kcontrol_new rt1316_snd_controls[] = {

	/* I2S Data Channel Selection */
	SOC_ENUM("RX Channel Select", rt1316_rx_data_ch_enum),

	/* XU24 Bypass Control */
	SOC_SINGLE("XU24 Bypass Switch",
		SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_XU24, RT1316_SDCA_CTL_BYPASS, 0), 0, 1, 0),

	/* Left/Right IV tag */
	SOC_SINGLE("Left V Tag Select", 0x3004, 0, 7, 0),
	SOC_SINGLE("Left I Tag Select", 0x3004, 4, 7, 0),
	SOC_SINGLE("Right V Tag Select", 0x3005, 0, 7, 0),
	SOC_SINGLE("Right I Tag Select", 0x3005, 4, 7, 0),

	/* IV mixer Control */
	SOC_DOUBLE("Isense Mixer Switch", 0xc605, 2, 0, 1, 1),
	SOC_DOUBLE("Vsense Mixer Switch", 0xc605, 3, 1, 1, 1),
};

static const struct snd_kcontrol_new rt1316_sto_dac =
	SOC_DAPM_DOUBLE_R("Switch",
		SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_FU21, RT1316_SDCA_CTL_FU_MUTE, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1316_SDCA_ENT_FU21, RT1316_SDCA_CTL_FU_MUTE, CH_R),
		0, 1, 1);

static const struct snd_soc_dapm_widget rt1316_dapm_widgets[] = {
	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("DP1RX", "DP1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP2TX", "DP2 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SWITCH("DAC", SND_SOC_NOPM, 0, 0, &rt1316_sto_dac),

	/* Output Lines */
	SND_SOC_DAPM_PGA_E("CLASS D", SND_SOC_NOPM, 0, 0, NULL, 0,
		rt1316_classd_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),

	SND_SOC_DAPM_SUPPLY("PDE 24", SND_SOC_NOPM, 0, 0,
		rt1316_pde24_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA("I Sense", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("V Sense", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SIGGEN("I Gen"),
	SND_SOC_DAPM_SIGGEN("V Gen"),
};

static const struct snd_soc_dapm_route rt1316_dapm_routes[] = {
	{ "DAC", "Switch", "DP1RX" },
	{ "CLASS D", NULL, "DAC" },
	{ "SPOL", NULL, "CLASS D" },
	{ "SPOR", NULL, "CLASS D" },

	{ "I Sense", NULL, "I Gen" },
	{ "V Sense", NULL, "V Gen" },
	{ "I Sense", NULL, "PDE 24" },
	{ "V Sense", NULL, "PDE 24" },
	{ "DP2TX", NULL, "I Sense" },
	{ "DP2TX", NULL, "V Sense" },
};

static int rt1316_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	struct sdw_stream_data *stream;

	if (!sdw_stream)
		return 0;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	stream->sdw_stream = sdw_stream;

	/* Use tx_mask or rx_mask to configure stream tag and set dma_data */
	if (direction == SNDRV_PCM_STREAM_PLAYBACK)
		dai->playback_dma_data = stream;
	else
		dai->capture_dma_data = stream;

	return 0;
}

static void rt1316_sdw_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sdw_stream_data *stream;

	stream = snd_soc_dai_get_dma_data(dai, substream);
	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(stream);
}

static int rt1316_sdw_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1316_sdw_priv *rt1316 =
		snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config;
	struct sdw_port_config port_config;
	enum sdw_data_direction direction;
	struct sdw_stream_data *stream;
	int retval, port, num_channels, ch_mask;

	dev_dbg(dai->dev, "%s %s", __func__, dai->name);
	stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!stream)
		return -EINVAL;

	if (!rt1316->sdw_slave)
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

	retval = sdw_stream_add_slave(rt1316->sdw_slave, &stream_config,
				&port_config, 1, stream->sdw_stream);
	if (retval) {
		dev_err(dai->dev, "Unable to configure port\n");
		return retval;
	}

	return 0;
}

static int rt1316_sdw_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1316_sdw_priv *rt1316 =
		snd_soc_component_get_drvdata(component);
	struct sdw_stream_data *stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt1316->sdw_slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt1316->sdw_slave, stream->sdw_stream);
	return 0;
}

/*
 * slave_ops: callbacks for get_clock_stop_mode, clock_stop and
 * port_prep are not defined for now
 */
static struct sdw_slave_ops rt1316_slave_ops = {
	.read_prop = rt1316_read_prop,
	.update_status = rt1316_update_status,
};

static int rt1316_sdw_component_probe(struct snd_soc_component *component)
{
	int ret;

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	return 0;
}

static const struct snd_soc_component_driver soc_component_sdw_rt1316 = {
	.probe = rt1316_sdw_component_probe,
	.controls = rt1316_snd_controls,
	.num_controls = ARRAY_SIZE(rt1316_snd_controls),
	.dapm_widgets = rt1316_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt1316_dapm_widgets),
	.dapm_routes = rt1316_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt1316_dapm_routes),
	.endianness = 1,
};

static const struct snd_soc_dai_ops rt1316_aif_dai_ops = {
	.hw_params = rt1316_sdw_hw_params,
	.hw_free	= rt1316_sdw_pcm_hw_free,
	.set_stream	= rt1316_set_sdw_stream,
	.shutdown	= rt1316_sdw_shutdown,
};

#define RT1316_STEREO_RATES SNDRV_PCM_RATE_48000
#define RT1316_FORMATS (SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver rt1316_sdw_dai[] = {
	{
		.name = "rt1316-aif",
		.playback = {
			.stream_name = "DP1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT1316_STEREO_RATES,
			.formats = RT1316_FORMATS,
		},
		.capture = {
			.stream_name = "DP2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT1316_STEREO_RATES,
			.formats = RT1316_FORMATS,
		},
		.ops = &rt1316_aif_dai_ops,
	},
};

static int rt1316_sdw_init(struct device *dev, struct regmap *regmap,
				struct sdw_slave *slave)
{
	struct rt1316_sdw_priv *rt1316;
	int ret;

	rt1316 = devm_kzalloc(dev, sizeof(*rt1316), GFP_KERNEL);
	if (!rt1316)
		return -ENOMEM;

	dev_set_drvdata(dev, rt1316);
	rt1316->sdw_slave = slave;
	rt1316->regmap = regmap;

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt1316->hw_init = false;
	rt1316->first_hw_init = false;

	ret =  devm_snd_soc_register_component(dev,
				&soc_component_sdw_rt1316,
				rt1316_sdw_dai,
				ARRAY_SIZE(rt1316_sdw_dai));

	dev_dbg(&slave->dev, "%s\n", __func__);

	return ret;
}

static int rt1316_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct regmap *regmap;

	/* Regmap Initialization */
	regmap = devm_regmap_init_sdw(slave, &rt1316_sdw_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return rt1316_sdw_init(&slave->dev, regmap, slave);
}

static int rt1316_sdw_remove(struct sdw_slave *slave)
{
	struct rt1316_sdw_priv *rt1316 = dev_get_drvdata(&slave->dev);

	if (rt1316->first_hw_init)
		pm_runtime_disable(&slave->dev);

	return 0;
}

static const struct sdw_device_id rt1316_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x1316, 0x3, 0x1, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt1316_id);

static int __maybe_unused rt1316_dev_suspend(struct device *dev)
{
	struct rt1316_sdw_priv *rt1316 = dev_get_drvdata(dev);

	if (!rt1316->hw_init)
		return 0;

	regcache_cache_only(rt1316->regmap, true);

	return 0;
}

#define RT1316_PROBE_TIMEOUT 5000

static int __maybe_unused rt1316_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt1316_sdw_priv *rt1316 = dev_get_drvdata(dev);
	unsigned long time;

	if (!rt1316->first_hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->initialization_complete,
				msecs_to_jiffies(RT1316_PROBE_TIMEOUT));
	if (!time) {
		dev_err(&slave->dev, "Initialization not complete, timed out\n");
		sdw_show_ping_status(slave->bus, true);

		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(rt1316->regmap, false);
	regcache_sync(rt1316->regmap);

	return 0;
}

static const struct dev_pm_ops rt1316_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(rt1316_dev_suspend, rt1316_dev_resume)
	SET_RUNTIME_PM_OPS(rt1316_dev_suspend, rt1316_dev_resume, NULL)
};

static struct sdw_driver rt1316_sdw_driver = {
	.driver = {
		.name = "rt1316-sdca",
		.owner = THIS_MODULE,
		.pm = &rt1316_pm,
	},
	.probe = rt1316_sdw_probe,
	.remove = rt1316_sdw_remove,
	.ops = &rt1316_slave_ops,
	.id_table = rt1316_id,
};
module_sdw_driver(rt1316_sdw_driver);

MODULE_DESCRIPTION("ASoC RT1316 driver SDCA SDW");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL");
