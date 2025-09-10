// SPDX-License-Identifier: GPL-2.0-only
//
// rt1318-sdw.c -- rt1318 SDCA ALSA SoC amplifier audio driver
//
// Copyright(c) 2022 Realtek Semiconductor Corp.
//
//
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/dmi.h>
#include <linux/firmware.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include "rt1318-sdw.h"

static const struct reg_sequence rt1318_blind_write[] = {
	{ 0xc001, 0x43 },
	{ 0xc003, 0xa2 },
	{ 0xc004, 0x44 },
	{ 0xc005, 0x44 },
	{ 0xc006, 0x33 },
	{ 0xc007, 0x64 },
	{ 0xc320, 0x20 },
	{ 0xf203, 0x18 },
	{ 0xf211, 0x00 },
	{ 0xf212, 0x26 },
	{ 0xf20d, 0x17 },
	{ 0xf214, 0x06 },
	{ 0xf20e, 0x00 },
	{ 0xf223, 0x7f },
	{ 0xf224, 0xdb },
	{ 0xf225, 0xee },
	{ 0xf226, 0x3f },
	{ 0xf227, 0x0f },
	{ 0xf21a, 0x78 },
	{ 0xf242, 0x3c },
	{ 0xc321, 0x0b },
	{ 0xc200, 0xd8 },
	{ 0xc201, 0x27 },
	{ 0xc202, 0x0f },
	{ 0xf800, 0x20 },
	{ 0xdf00, 0x10 },
	{ 0xdf5f, 0x01 },
	{ 0xdf60, 0xa7 },
	{ 0xc400, 0x0e },
	{ 0xc401, 0x43 },
	{ 0xc402, 0xe0 },
	{ 0xc403, 0x00 },
	{ 0xc404, 0x4c },
	{ 0xc407, 0x02 },
	{ 0xc408, 0x3f },
	{ 0xc300, 0x01 },
	{ 0xc206, 0x78 },
	{ 0xc203, 0x84 },
	{ 0xc120, 0xc0 },
	{ 0xc121, 0x03 },
	{ 0xe000, 0x88 },
	{ 0xc321, 0x09 },
	{ 0xc322, 0x01 },
	{ 0xe706, 0x0f },
	{ 0xe707, 0x30 },
	{ 0xe806, 0x0f },
	{ 0xe807, 0x30 },
	{ 0xed00, 0xb0 },
	{ 0xce04, 0x02 },
	{ 0xce05, 0x63 },
	{ 0xce06, 0x68 },
	{ 0xce07, 0x07 },
	{ 0xcf04, 0x02 },
	{ 0xcf05, 0x63 },
	{ 0xcf06, 0x68 },
	{ 0xcf07, 0x07 },
	{ 0xce60, 0xe3 },
	{ 0xc130, 0x51 },
	{ 0xf102, 0x00 },
	{ 0xf103, 0x00 },
	{ 0xf104, 0xf5 },
	{ 0xf105, 0x06 },
	{ 0xf109, 0x9b },
	{ 0xf10a, 0x0b },
	{ 0xf10b, 0x4c },
	{ 0xf10b, 0x5c },
	{ 0xf102, 0x00 },
	{ 0xf103, 0x00 },
	{ 0xf104, 0xf5 },
	{ 0xf105, 0x0b },
	{ 0xf109, 0x03 },
	{ 0xf10a, 0x0b },
	{ 0xf10b, 0x4c },
	{ 0xf10b, 0x5c },
	{ 0xf102, 0x00 },
	{ 0xf103, 0x00 },
	{ 0xf104, 0xf5 },
	{ 0xf105, 0x0c },
	{ 0xf109, 0x7f },
	{ 0xf10a, 0x0b },
	{ 0xf10b, 0x4c },
	{ 0xf10b, 0x5c },

	{ 0xe604, 0x00 },
	{ 0xdb00, 0x0c },
	{ 0xdd00, 0x0c },
	{ 0xdc19, 0x00 },
	{ 0xdc1a, 0xff },
	{ 0xdc1b, 0xff },
	{ 0xdc1c, 0xff },
	{ 0xdc1d, 0x00 },
	{ 0xdc1e, 0x00 },
	{ 0xdc1f, 0x00 },
	{ 0xdc20, 0xff },
	{ 0xde19, 0x00 },
	{ 0xde1a, 0xff },
	{ 0xde1b, 0xff },
	{ 0xde1c, 0xff },
	{ 0xde1d, 0x00 },
	{ 0xde1e, 0x00 },
	{ 0xde1f, 0x00 },
	{ 0xde20, 0xff },
	{ 0xdb32, 0x00 },
	{ 0xdd32, 0x00 },
	{ 0xdb33, 0x0a },
	{ 0xdd33, 0x0a },
	{ 0xdb34, 0x1a },
	{ 0xdd34, 0x1a },
	{ 0xdb17, 0xef },
	{ 0xdd17, 0xef },
	{ 0xdba7, 0x00 },
	{ 0xdba8, 0x64 },
	{ 0xdda7, 0x00 },
	{ 0xdda8, 0x64 },
	{ 0xdb19, 0x40 },
	{ 0xdd19, 0x40 },
	{ 0xdb00, 0x4c },
	{ 0xdb01, 0x79 },
	{ 0xdd01, 0x79 },
	{ 0xdb04, 0x05 },
	{ 0xdb05, 0x03 },
	{ 0xdd04, 0x05 },
	{ 0xdd05, 0x03 },
	{ 0xdbbb, 0x09 },
	{ 0xdbbc, 0x30 },
	{ 0xdbbd, 0xf0 },
	{ 0xdbbe, 0xf1 },
	{ 0xddbb, 0x09 },
	{ 0xddbc, 0x30 },
	{ 0xddbd, 0xf0 },
	{ 0xddbe, 0xf1 },
	{ 0xdb01, 0x79 },
	{ 0xdd01, 0x79 },
	{ 0xdc52, 0xef },
	{ 0xde52, 0xef },
	{ 0x2f55, 0x22 },
};

static const struct reg_default rt1318_reg_defaults[] = {
	{ 0x3000, 0x00 },
	{ 0x3004, 0x01 },
	{ 0x3005, 0x23 },
	{ 0x3202, 0x00 },
	{ 0x3203, 0x01 },
	{ 0x3206, 0x00 },
	{ 0xc000, 0x00 },
	{ 0xc001, 0x43 },
	{ 0xc003, 0x22 },
	{ 0xc004, 0x44 },
	{ 0xc005, 0x44 },
	{ 0xc006, 0x33 },
	{ 0xc007, 0x64 },
	{ 0xc008, 0x05 },
	{ 0xc00a, 0xfc },
	{ 0xc00b, 0x0f },
	{ 0xc00c, 0x0e },
	{ 0xc00d, 0xef },
	{ 0xc00e, 0xe5 },
	{ 0xc00f, 0xff },
	{ 0xc120, 0xc0 },
	{ 0xc121, 0x00 },
	{ 0xc122, 0x00 },
	{ 0xc123, 0x14 },
	{ 0xc125, 0x00 },
	{ 0xc200, 0x00 },
	{ 0xc201, 0x00 },
	{ 0xc202, 0x00 },
	{ 0xc203, 0x04 },
	{ 0xc204, 0x00 },
	{ 0xc205, 0x00 },
	{ 0xc206, 0x68 },
	{ 0xc207, 0x70 },
	{ 0xc208, 0x00 },
	{ 0xc20a, 0x00 },
	{ 0xc20b, 0x01 },
	{ 0xc20c, 0x7f },
	{ 0xc20d, 0x01 },
	{ 0xc20e, 0x7f },
	{ 0xc300, 0x00 },
	{ 0xc301, 0x00 },
	{ 0xc303, 0x80 },
	{ 0xc320, 0x00 },
	{ 0xc321, 0x09 },
	{ 0xc322, 0x02 },
	{ 0xc410, 0x04 },
	{ 0xc430, 0x00 },
	{ 0xc431, 0x00 },
	{ 0xca00, 0x10 },
	{ 0xca01, 0x00 },
	{ 0xca02, 0x0b },
	{ 0xca10, 0x10 },
	{ 0xca11, 0x00 },
	{ 0xca12, 0x0b },
	{ 0xdd93, 0x00 },
	{ 0xdd94, 0x64 },
	{ 0xe300, 0xa0 },
	{ 0xed00, 0x80 },
	{ 0xed01, 0x0f },
	{ 0xed02, 0xff },
	{ 0xed03, 0x00 },
	{ 0xed04, 0x00 },
	{ 0xed05, 0x0f },
	{ 0xed06, 0xff },
	{ 0xf010, 0x10 },
	{ 0xf011, 0xec },
	{ 0xf012, 0x68 },
	{ 0xf013, 0x21 },
	{ 0xf800, 0x00 },
	{ 0xf801, 0x12 },
	{ 0xf802, 0xe0 },
	{ 0xf803, 0x2f },
	{ 0xf804, 0x00 },
	{ 0xf805, 0x00 },
	{ 0xf806, 0x07 },
	{ 0xf807, 0xff },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_UDMPU21, RT1318_SDCA_CTL_UDMPU_CLUSTER, 0), 0x00 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_FU21, RT1318_SDCA_CTL_FU_MUTE, CH_L), 0x01 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_FU21, RT1318_SDCA_CTL_FU_MUTE, CH_R), 0x01 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_PDE23, RT1318_SDCA_CTL_REQ_POWER_STATE, 0), 0x03 },
	{ SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_CS21, RT1318_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), 0x09 },
};

static bool rt1318_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2f55:
	case 0x3000:
	case 0x3004 ... 0x3005:
	case 0x3202 ... 0x3203:
	case 0x3206:
	case 0xc000 ... 0xc00f:
	case 0xc120 ... 0xc125:
	case 0xc200 ... 0xc20e:
	case 0xc300 ... 0xc303:
	case 0xc320 ... 0xc322:
	case 0xc410:
	case 0xc430 ... 0xc431:
	case 0xca00 ... 0xca02:
	case 0xca10 ... 0xca12:
	case 0xcb00 ... 0xcb0b:
	case 0xcc00 ... 0xcce5:
	case 0xcd00 ... 0xcde5:
	case 0xce00 ... 0xce6a:
	case 0xcf00 ... 0xcf53:
	case 0xd000 ... 0xd0cc:
	case 0xd100 ... 0xd1b9:
	case 0xdb00 ... 0xdc53:
	case 0xdd00 ... 0xde53:
	case 0xdf00 ... 0xdf6b:
	case 0xe300:
	case 0xeb00 ... 0xebcc:
	case 0xec00 ... 0xecb9:
	case 0xed00 ... 0xed06:
	case 0xf010 ... 0xf014:
	case 0xf800 ... 0xf807:
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_UDMPU21, RT1318_SDCA_CTL_UDMPU_CLUSTER, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_FU21, RT1318_SDCA_CTL_FU_MUTE, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_FU21, RT1318_SDCA_CTL_FU_MUTE, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_PDE23, RT1318_SDCA_CTL_REQ_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_CS21, RT1318_SDCA_CTL_SAMPLE_FREQ_INDEX, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_SAPU, RT1318_SDCA_CTL_SAPU_PROTECTION_MODE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_SAPU, RT1318_SDCA_CTL_SAPU_PROTECTION_STATUS, 0):
		return true;
	default:
		return false;
	}
}

static bool rt1318_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2f55:
	case 0x3000 ... 0x3001:
	case 0xc000:
	case 0xc301:
	case 0xc410:
	case 0xc430 ... 0xc431:
	case 0xdb06:
	case 0xdb12:
	case 0xdb1d ... 0xdb1f:
	case 0xdb35:
	case 0xdb37:
	case 0xdb8a ... 0xdb92:
	case 0xdbc5 ... 0xdbc8:
	case 0xdc2b ... 0xdc49:
	case 0xdd0b:
	case 0xdd12:
	case 0xdd1d ... 0xdd1f:
	case 0xdd35:
	case 0xdd8a ... 0xdd92:
	case 0xddc5 ... 0xddc8:
	case 0xde2b ... 0xde44:
	case 0xdf4a ... 0xdf55:
	case 0xe224 ... 0xe23b:
	case 0xea01:
	case 0xebc5:
	case 0xebc8:
	case 0xebcb ... 0xebcc:
	case 0xed03 ... 0xed06:
	case 0xf010 ... 0xf014:
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_SAPU, RT1318_SDCA_CTL_SAPU_PROTECTION_MODE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_SAPU, RT1318_SDCA_CTL_SAPU_PROTECTION_STATUS, 0):
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rt1318_sdw_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = rt1318_readable_register,
	.volatile_reg = rt1318_volatile_register,
	.max_register = 0x41081488,
	.reg_defaults = rt1318_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt1318_reg_defaults),
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt1318_read_prop(struct sdw_slave *slave)
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

	/* first we need to allocate memory for set bits in port lists */
	prop->source_ports = BIT(2);
	prop->sink_ports = BIT(1);

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

	return 0;
}

static int rt1318_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt1318_sdw_priv *rt1318 = dev_get_drvdata(dev);

	if (rt1318->hw_init)
		return 0;

	regcache_cache_only(rt1318->regmap, false);
	if (rt1318->first_hw_init) {
		regcache_cache_bypass(rt1318->regmap, true);
	} else {
		/*
		 * PM runtime status is marked as 'active' only when a Slave reports as Attached
		 */
		/* update count of parent 'active' children */
		pm_runtime_set_active(&slave->dev);
	}

	pm_runtime_get_noresume(&slave->dev);

	/* blind write */
	regmap_multi_reg_write(rt1318->regmap, rt1318_blind_write,
		ARRAY_SIZE(rt1318_blind_write));

	if (rt1318->first_hw_init) {
		regcache_cache_bypass(rt1318->regmap, false);
		regcache_mark_dirty(rt1318->regmap);
	}

	/* Mark Slave initialization complete */
	rt1318->first_hw_init = true;
	rt1318->hw_init = true;

	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "%s hw_init complete\n", __func__);
	return 0;
}

static int rt1318_update_status(struct sdw_slave *slave,
					enum sdw_slave_status status)
{
	struct  rt1318_sdw_priv *rt1318 = dev_get_drvdata(&slave->dev);

	if (status == SDW_SLAVE_UNATTACHED)
		rt1318->hw_init = false;

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt1318->hw_init || status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt1318_io_init(&slave->dev, slave);
}

static int rt1318_classd_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt1318_sdw_priv *rt1318 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt1318->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_PDE23,
				RT1318_SDCA_CTL_REQ_POWER_STATE, 0),
				ps0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt1318->regmap,
			SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_PDE23,
				RT1318_SDCA_CTL_REQ_POWER_STATE, 0),
				ps3);
		break;

	default:
		break;
	}

	return 0;
}

static const char * const rt1318_rx_data_ch_select[] = {
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

static SOC_ENUM_SINGLE_DECL(rt1318_rx_data_ch_enum,
	SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_UDMPU21, RT1318_SDCA_CTL_UDMPU_CLUSTER, 0), 0,
	rt1318_rx_data_ch_select);

static const struct snd_kcontrol_new rt1318_snd_controls[] = {

	/* UDMPU Cluster Selection */
	SOC_ENUM("RX Channel Select", rt1318_rx_data_ch_enum),
};

static const struct snd_kcontrol_new rt1318_sto_dac =
	SOC_DAPM_DOUBLE_R("Switch",
		SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_FU21, RT1318_SDCA_CTL_FU_MUTE, CH_L),
		SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_FU21, RT1318_SDCA_CTL_FU_MUTE, CH_R),
		0, 1, 1);

static const struct snd_soc_dapm_widget rt1318_dapm_widgets[] = {
	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("DP1RX", "DP1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP2TX", "DP2 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SWITCH("DAC", SND_SOC_NOPM, 0, 0, &rt1318_sto_dac),

	/* Output */
	SND_SOC_DAPM_PGA_E("CLASS D", SND_SOC_NOPM, 0, 0, NULL, 0,
		rt1318_classd_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),
	/* Input */
	SND_SOC_DAPM_PGA("FB Data", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SIGGEN("FB Gen"),
};

static const struct snd_soc_dapm_route rt1318_dapm_routes[] = {
	{ "DAC", "Switch", "DP1RX" },
	{ "CLASS D", NULL, "DAC" },
	{ "SPOL", NULL, "CLASS D" },
	{ "SPOR", NULL, "CLASS D" },

	{ "FB Data", NULL, "FB Gen" },
	{ "DP2TX", NULL, "FB Data" },
};

static int rt1318_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void rt1318_sdw_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int rt1318_sdw_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1318_sdw_priv *rt1318 =
		snd_soc_component_get_drvdata(component);
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

	if (!rt1318->sdw_slave)
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

	retval = sdw_stream_add_slave(rt1318->sdw_slave, &stream_config,
				&port_config, 1, sdw_stream);
	if (retval) {
		dev_err(dai->dev, "%s: Unable to configure port\n", __func__);
		return retval;
	}

	/* sampling rate configuration */
	switch (params_rate(params)) {
	case 16000:
		sampling_rate = RT1318_SDCA_RATE_16000HZ;
		break;
	case 32000:
		sampling_rate = RT1318_SDCA_RATE_32000HZ;
		break;
	case 44100:
		sampling_rate = RT1318_SDCA_RATE_44100HZ;
		break;
	case 48000:
		sampling_rate = RT1318_SDCA_RATE_48000HZ;
		break;
	case 96000:
		sampling_rate = RT1318_SDCA_RATE_96000HZ;
		break;
	case 192000:
		sampling_rate = RT1318_SDCA_RATE_192000HZ;
		break;
	default:
		dev_err(component->dev, "%s: Rate %d is not supported\n",
			__func__, params_rate(params));
		return -EINVAL;
	}

	/* set sampling frequency */
	regmap_write(rt1318->regmap,
		SDW_SDCA_CTL(FUNC_NUM_SMART_AMP, RT1318_SDCA_ENT_CS21, RT1318_SDCA_CTL_SAMPLE_FREQ_INDEX, 0),
		sampling_rate);

	return 0;
}

static int rt1318_sdw_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1318_sdw_priv *rt1318 =
		snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *sdw_stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt1318->sdw_slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt1318->sdw_slave, sdw_stream);
	return 0;
}

/*
 * slave_ops: callbacks for get_clock_stop_mode, clock_stop and
 * port_prep are not defined for now
 */
static const struct sdw_slave_ops rt1318_slave_ops = {
	.read_prop = rt1318_read_prop,
	.update_status = rt1318_update_status,
};

static int rt1318_sdw_component_probe(struct snd_soc_component *component)
{
	int ret;
	struct rt1318_sdw_priv *rt1318 = snd_soc_component_get_drvdata(component);

	rt1318->component = component;

	if (!rt1318->first_hw_init)
		return 0;

	ret = pm_runtime_resume(component->dev);
	dev_dbg(&rt1318->sdw_slave->dev, "%s pm_runtime_resume, ret=%d", __func__, ret);
	if (ret < 0 && ret != -EACCES)
		return ret;

	return 0;
}

static const struct snd_soc_component_driver soc_component_sdw_rt1318 = {
	.probe = rt1318_sdw_component_probe,
	.controls = rt1318_snd_controls,
	.num_controls = ARRAY_SIZE(rt1318_snd_controls),
	.dapm_widgets = rt1318_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt1318_dapm_widgets),
	.dapm_routes = rt1318_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt1318_dapm_routes),
	.endianness = 1,
};

static const struct snd_soc_dai_ops rt1318_aif_dai_ops = {
	.hw_params = rt1318_sdw_hw_params,
	.hw_free	= rt1318_sdw_pcm_hw_free,
	.set_stream	= rt1318_set_sdw_stream,
	.shutdown	= rt1318_sdw_shutdown,
};

#define RT1318_STEREO_RATES (SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define RT1318_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver rt1318_sdw_dai[] = {
	{
		.name = "rt1318-aif",
		.playback = {
			.stream_name = "DP1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT1318_STEREO_RATES,
			.formats = RT1318_FORMATS,
		},
		.capture = {
			.stream_name = "DP2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT1318_STEREO_RATES,
			.formats = RT1318_FORMATS,
		},
		.ops = &rt1318_aif_dai_ops,
	},
};

static int rt1318_sdw_init(struct device *dev, struct regmap *regmap,
				struct sdw_slave *slave)
{
	struct rt1318_sdw_priv *rt1318;
	int ret;

	rt1318 = devm_kzalloc(dev, sizeof(*rt1318), GFP_KERNEL);
	if (!rt1318)
		return -ENOMEM;

	dev_set_drvdata(dev, rt1318);
	rt1318->sdw_slave = slave;
	rt1318->regmap = regmap;

	regcache_cache_only(rt1318->regmap, true);

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt1318->hw_init = false;
	rt1318->first_hw_init = false;

	ret =  devm_snd_soc_register_component(dev,
				&soc_component_sdw_rt1318,
				rt1318_sdw_dai,
				ARRAY_SIZE(rt1318_sdw_dai));
	if (ret < 0)
		return ret;

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

	dev_dbg(dev, "%s\n", __func__);

	return ret;
}

static int rt1318_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct regmap *regmap;

	/* Regmap Initialization */
	regmap = devm_regmap_init_sdw(slave, &rt1318_sdw_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return rt1318_sdw_init(&slave->dev, regmap, slave);
}

static int rt1318_sdw_remove(struct sdw_slave *slave)
{
	pm_runtime_disable(&slave->dev);

	return 0;
}

static const struct sdw_device_id rt1318_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x1318, 0x3, 0x1, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt1318_id);

static int rt1318_dev_suspend(struct device *dev)
{
	struct rt1318_sdw_priv *rt1318 = dev_get_drvdata(dev);

	if (!rt1318->hw_init)
		return 0;

	regcache_cache_only(rt1318->regmap, true);
	return 0;
}

#define RT1318_PROBE_TIMEOUT 5000

static int rt1318_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt1318_sdw_priv *rt1318 = dev_get_drvdata(dev);
	unsigned long time;

	if (!rt1318->first_hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->initialization_complete,
				msecs_to_jiffies(RT1318_PROBE_TIMEOUT));
	if (!time) {
		dev_err(&slave->dev, "%s: Initialization not complete, timed out\n", __func__);
		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(rt1318->regmap, false);
	regcache_sync(rt1318->regmap);

	return 0;
}

static const struct dev_pm_ops rt1318_pm = {
	SYSTEM_SLEEP_PM_OPS(rt1318_dev_suspend, rt1318_dev_resume)
	RUNTIME_PM_OPS(rt1318_dev_suspend, rt1318_dev_resume, NULL)
};

static struct sdw_driver rt1318_sdw_driver = {
	.driver = {
		.name = "rt1318-sdca",
		.pm = pm_ptr(&rt1318_pm),
	},
	.probe = rt1318_sdw_probe,
	.remove = rt1318_sdw_remove,
	.ops = &rt1318_slave_ops,
	.id_table = rt1318_id,
};
module_sdw_driver(rt1318_sdw_driver);

MODULE_DESCRIPTION("ASoC RT1318 driver SDCA SDW");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL");
