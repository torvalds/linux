// SPDX-License-Identifier: GPL-2.0
//
// rt1308-sdw.c -- rt1308 ALSA SoC audio driver
//
// Copyright(c) 2019 Realtek Semiconductor Corp.
//
//
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/mod_devicetable.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/sdw.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "rt1308.h"
#include "rt1308-sdw.h"

static bool rt1308_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00e0:
	case 0x00f0:
	case 0x2f01 ... 0x2f07:
	case 0x3000 ... 0x3001:
	case 0x3004 ... 0x3005:
	case 0x3008:
	case 0x300a:
	case 0xc000 ... 0xcff3:
		return true;
	default:
		return false;
	}
}

static bool rt1308_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2f01 ... 0x2f07:
	case 0x3000 ... 0x3001:
	case 0x3004 ... 0x3005:
	case 0x3008:
	case 0x300a:
	case 0xc000:
	case 0xc710:
	case 0xc860 ... 0xc863:
	case 0xc870 ... 0xc873:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rt1308_sdw_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = rt1308_readable_register,
	.volatile_reg = rt1308_volatile_register,
	.max_register = 0xcfff,
	.reg_defaults = rt1308_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt1308_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

/* Bus clock frequency */
#define RT1308_CLK_FREQ_9600000HZ 9600000
#define RT1308_CLK_FREQ_12000000HZ 12000000
#define RT1308_CLK_FREQ_6000000HZ 6000000
#define RT1308_CLK_FREQ_4800000HZ 4800000
#define RT1308_CLK_FREQ_2400000HZ 2400000
#define RT1308_CLK_FREQ_12288000HZ 12288000

static int rt1308_clock_config(struct device *dev)
{
	struct rt1308_sdw_priv *rt1308 = dev_get_drvdata(dev);
	unsigned int clk_freq, value;

	clk_freq = (rt1308->params.curr_dr_freq >> 1);

	switch (clk_freq) {
	case RT1308_CLK_FREQ_12000000HZ:
		value = 0x0;
		break;
	case RT1308_CLK_FREQ_6000000HZ:
		value = 0x1;
		break;
	case RT1308_CLK_FREQ_9600000HZ:
		value = 0x2;
		break;
	case RT1308_CLK_FREQ_4800000HZ:
		value = 0x3;
		break;
	case RT1308_CLK_FREQ_2400000HZ:
		value = 0x4;
		break;
	case RT1308_CLK_FREQ_12288000HZ:
		value = 0x5;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(rt1308->regmap, 0xe0, value);
	regmap_write(rt1308->regmap, 0xf0, value);

	dev_dbg(dev, "%s complete, clk_freq=%d\n", __func__, clk_freq);

	return 0;
}

static int rt1308_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval, i;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;

	prop->paging_support = true;

	/* first we need to allocate memory for set bits in port lists */
	prop->source_ports = 0x00; /* BITMAP: 00010100 (not enable yet) */
	prop->sink_ports = 0x2; /* BITMAP:  00000010 */

	/* for sink */
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

	/* set the timeout values */
	prop->clk_stop_timeout = 20;

	dev_dbg(&slave->dev, "%s\n", __func__);

	return 0;
}

static void rt1308_apply_calib_params(struct rt1308_sdw_priv *rt1308)
{
	unsigned int efuse_m_btl_l, efuse_m_btl_r, tmp;
	unsigned int efuse_c_btl_l, efuse_c_btl_r;

	/* read efuse to apply calibration parameters */
	regmap_write(rt1308->regmap, 0xc7f0, 0x04);
	regmap_write(rt1308->regmap, 0xc7f1, 0xfe);
	msleep(100);
	regmap_write(rt1308->regmap, 0xc7f0, 0x44);
	msleep(20);
	regmap_write(rt1308->regmap, 0xc240, 0x10);

	regmap_read(rt1308->regmap, 0xc861, &tmp);
	efuse_m_btl_l = tmp;
	regmap_read(rt1308->regmap, 0xc860, &tmp);
	efuse_m_btl_l = efuse_m_btl_l | (tmp << 8);
	regmap_read(rt1308->regmap, 0xc863, &tmp);
	efuse_c_btl_l = tmp;
	regmap_read(rt1308->regmap, 0xc862, &tmp);
	efuse_c_btl_l = efuse_c_btl_l | (tmp << 8);
	regmap_read(rt1308->regmap, 0xc871, &tmp);
	efuse_m_btl_r = tmp;
	regmap_read(rt1308->regmap, 0xc870, &tmp);
	efuse_m_btl_r = efuse_m_btl_r | (tmp << 8);
	regmap_read(rt1308->regmap, 0xc873, &tmp);
	efuse_c_btl_r = tmp;
	regmap_read(rt1308->regmap, 0xc872, &tmp);
	efuse_c_btl_r = efuse_c_btl_r | (tmp << 8);
	dev_dbg(&rt1308->sdw_slave->dev, "%s m_btl_l=0x%x, m_btl_r=0x%x\n", __func__,
		efuse_m_btl_l, efuse_m_btl_r);
	dev_dbg(&rt1308->sdw_slave->dev, "%s c_btl_l=0x%x, c_btl_r=0x%x\n", __func__,
		efuse_c_btl_l, efuse_c_btl_r);
}

static void rt1308_apply_bq_params(struct rt1308_sdw_priv *rt1308)
{
	unsigned int i, reg, data;

	for (i = 0; i < rt1308->bq_params_cnt; i += 3) {
		reg = rt1308->bq_params[i] | (rt1308->bq_params[i + 1] << 8);
		data = rt1308->bq_params[i + 2];
		regmap_write(rt1308->regmap, reg, data);
	}
}

static int rt1308_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt1308_sdw_priv *rt1308 = dev_get_drvdata(dev);
	int ret = 0;
	unsigned int tmp;

	if (rt1308->hw_init)
		return 0;

	if (rt1308->first_hw_init) {
		regcache_cache_only(rt1308->regmap, false);
		regcache_cache_bypass(rt1308->regmap, true);
	}

	/*
	 * PM runtime is only enabled when a Slave reports as Attached
	 */
	if (!rt1308->first_hw_init) {
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
	regmap_write(rt1308->regmap, RT1308_SDW_RESET, 0);

	regmap_read(rt1308->regmap, 0xc710, &tmp);
	rt1308->hw_ver = tmp;
	dev_dbg(dev, "%s, hw_ver=0x%x\n", __func__, rt1308->hw_ver);

	/* initial settings */
	regmap_write(rt1308->regmap, 0xc103, 0xc0);
	regmap_write(rt1308->regmap, 0xc030, 0x17);
	regmap_write(rt1308->regmap, 0xc031, 0x81);
	regmap_write(rt1308->regmap, 0xc032, 0x26);
	regmap_write(rt1308->regmap, 0xc040, 0x80);
	regmap_write(rt1308->regmap, 0xc041, 0x80);
	regmap_write(rt1308->regmap, 0xc042, 0x06);
	regmap_write(rt1308->regmap, 0xc052, 0x0a);
	regmap_write(rt1308->regmap, 0xc080, 0x0a);
	regmap_write(rt1308->regmap, 0xc060, 0x02);
	regmap_write(rt1308->regmap, 0xc061, 0x75);
	regmap_write(rt1308->regmap, 0xc062, 0x05);
	regmap_write(rt1308->regmap, 0xc171, 0x07);
	regmap_write(rt1308->regmap, 0xc173, 0x0d);
	if (rt1308->hw_ver == RT1308_VER_C) {
		regmap_write(rt1308->regmap, 0xc311, 0x7f);
		regmap_write(rt1308->regmap, 0xc300, 0x09);
	} else {
		regmap_write(rt1308->regmap, 0xc311, 0x4f);
		regmap_write(rt1308->regmap, 0xc300, 0x0b);
	}
	regmap_write(rt1308->regmap, 0xc900, 0x5a);
	regmap_write(rt1308->regmap, 0xc1a0, 0x84);
	regmap_write(rt1308->regmap, 0xc1a1, 0x01);
	regmap_write(rt1308->regmap, 0xc360, 0x78);
	regmap_write(rt1308->regmap, 0xc361, 0x87);
	regmap_write(rt1308->regmap, 0xc0a1, 0x71);
	regmap_write(rt1308->regmap, 0xc210, 0x00);
	regmap_write(rt1308->regmap, 0xc070, 0x00);
	regmap_write(rt1308->regmap, 0xc100, 0xd7);
	regmap_write(rt1308->regmap, 0xc101, 0xd7);

	if (rt1308->first_hw_init) {
		regcache_cache_bypass(rt1308->regmap, false);
		regcache_mark_dirty(rt1308->regmap);
	} else
		rt1308->first_hw_init = true;

	/* Mark Slave initialization complete */
	rt1308->hw_init = true;

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "%s hw_init complete\n", __func__);

	return ret;
}

static int rt1308_update_status(struct sdw_slave *slave,
					enum sdw_slave_status status)
{
	struct  rt1308_sdw_priv *rt1308 = dev_get_drvdata(&slave->dev);

	/* Update the status */
	rt1308->status = status;

	if (status == SDW_SLAVE_UNATTACHED)
		rt1308->hw_init = false;

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt1308->hw_init || rt1308->status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt1308_io_init(&slave->dev, slave);
}

static int rt1308_bus_config(struct sdw_slave *slave,
				struct sdw_bus_params *params)
{
	struct rt1308_sdw_priv *rt1308 = dev_get_drvdata(&slave->dev);
	int ret;

	memcpy(&rt1308->params, params, sizeof(*params));

	ret = rt1308_clock_config(&slave->dev);
	if (ret < 0)
		dev_err(&slave->dev, "Invalid clk config");

	return ret;
}

static int rt1308_interrupt_callback(struct sdw_slave *slave,
					struct sdw_slave_intr_status *status)
{
	dev_dbg(&slave->dev,
		"%s control_port_stat=%x", __func__, status->control_port);

	return 0;
}

static int rt1308_classd_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt1308_sdw_priv *rt1308 =
		snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(30);
		snd_soc_component_update_bits(component,
			RT1308_SDW_OFFSET | (RT1308_POWER_STATUS << 4),
			0x3,	0x3);
		msleep(40);
		rt1308_apply_calib_params(rt1308);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component,
			RT1308_SDW_OFFSET | (RT1308_POWER_STATUS << 4),
			0x3, 0);
		usleep_range(150000, 200000);
		break;

	default:
		break;
	}

	return 0;
}

static const char * const rt1308_rx_data_ch_select[] = {
	"LR",
	"LL",
	"RL",
	"RR",
};

static SOC_ENUM_SINGLE_DECL(rt1308_rx_data_ch_enum,
	RT1308_SDW_OFFSET | (RT1308_DATA_PATH << 4), 0,
	rt1308_rx_data_ch_select);

static const struct snd_kcontrol_new rt1308_snd_controls[] = {

	/* I2S Data Channel Selection */
	SOC_ENUM("RX Channel Select", rt1308_rx_data_ch_enum),
};

static const struct snd_kcontrol_new rt1308_sto_dac_l =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch",
		RT1308_SDW_OFFSET_BYTE3 | (RT1308_DAC_SET << 4),
		RT1308_DVOL_MUTE_L_EN_SFT, 1, 1);

static const struct snd_kcontrol_new rt1308_sto_dac_r =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch",
		RT1308_SDW_OFFSET_BYTE3 | (RT1308_DAC_SET << 4),
		RT1308_DVOL_MUTE_R_EN_SFT, 1, 1);

static const struct snd_soc_dapm_widget rt1308_dapm_widgets[] = {
	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "DP1 Playback", 0, SND_SOC_NOPM, 0, 0),

	/* Supply Widgets */
	SND_SOC_DAPM_SUPPLY("MBIAS20U",
		RT1308_SDW_OFFSET | (RT1308_POWER << 4),	7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ALDO",
		RT1308_SDW_OFFSET | (RT1308_POWER << 4),	6, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DBG",
		RT1308_SDW_OFFSET | (RT1308_POWER << 4),	5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DACL",
		RT1308_SDW_OFFSET | (RT1308_POWER << 4),	4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLK25M",
		RT1308_SDW_OFFSET | (RT1308_POWER << 4),	2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC_R",
		RT1308_SDW_OFFSET | (RT1308_POWER << 4),	1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC_L",
		RT1308_SDW_OFFSET | (RT1308_POWER << 4),	0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Power",
		RT1308_SDW_OFFSET | (RT1308_POWER << 4),	3, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DLDO",
		RT1308_SDW_OFFSET_BYTE1 | (RT1308_POWER << 4),	5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VREF",
		RT1308_SDW_OFFSET_BYTE1 | (RT1308_POWER << 4),	4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIXER_R",
		RT1308_SDW_OFFSET_BYTE1 | (RT1308_POWER << 4),	2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIXER_L",
		RT1308_SDW_OFFSET_BYTE1 | (RT1308_POWER << 4),	1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MBIAS4U",
		RT1308_SDW_OFFSET_BYTE1 | (RT1308_POWER << 4),	0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("PLL2_LDO",
		RT1308_SDW_OFFSET_BYTE2 | (RT1308_POWER << 4), 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2B",
		RT1308_SDW_OFFSET_BYTE2 | (RT1308_POWER << 4), 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2F",
		RT1308_SDW_OFFSET_BYTE2 | (RT1308_POWER << 4), 2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2F2",
		RT1308_SDW_OFFSET_BYTE2 | (RT1308_POWER << 4), 1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2B2",
		RT1308_SDW_OFFSET_BYTE2 | (RT1308_POWER << 4), 0, 0, NULL, 0),

	/* Digital Interface */
	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SWITCH("DAC L", SND_SOC_NOPM, 0, 0, &rt1308_sto_dac_l),
	SND_SOC_DAPM_SWITCH("DAC R", SND_SOC_NOPM, 0, 0, &rt1308_sto_dac_r),

	/* Output Lines */
	SND_SOC_DAPM_PGA_E("CLASS D", SND_SOC_NOPM, 0, 0, NULL, 0,
		rt1308_classd_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),
};

static const struct snd_soc_dapm_route rt1308_dapm_routes[] = {

	{ "DAC", NULL, "AIF1RX" },

	{ "DAC", NULL, "MBIAS20U" },
	{ "DAC", NULL, "ALDO" },
	{ "DAC", NULL, "DBG" },
	{ "DAC", NULL, "DACL" },
	{ "DAC", NULL, "CLK25M" },
	{ "DAC", NULL, "ADC_R" },
	{ "DAC", NULL, "ADC_L" },
	{ "DAC", NULL, "DLDO" },
	{ "DAC", NULL, "VREF" },
	{ "DAC", NULL, "MIXER_R" },
	{ "DAC", NULL, "MIXER_L" },
	{ "DAC", NULL, "MBIAS4U" },
	{ "DAC", NULL, "PLL2_LDO" },
	{ "DAC", NULL, "PLL2B" },
	{ "DAC", NULL, "PLL2F" },
	{ "DAC", NULL, "PLL2F2" },
	{ "DAC", NULL, "PLL2B2" },

	{ "DAC L", "Switch", "DAC" },
	{ "DAC R", "Switch", "DAC" },
	{ "DAC L", NULL, "DAC Power" },
	{ "DAC R", NULL, "DAC Power" },

	{ "CLASS D", NULL, "DAC L" },
	{ "CLASS D", NULL, "DAC R" },
	{ "SPOL", NULL, "CLASS D" },
	{ "SPOR", NULL, "CLASS D" },
};

static int rt1308_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
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

static void rt1308_sdw_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sdw_stream_data *stream;

	stream = snd_soc_dai_get_dma_data(dai, substream);
	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(stream);
}

static int rt1308_sdw_set_tdm_slot(struct snd_soc_dai *dai,
				   unsigned int tx_mask,
				   unsigned int rx_mask,
				   int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct rt1308_sdw_priv *rt1308 =
		snd_soc_component_get_drvdata(component);

	if (tx_mask)
		return -EINVAL;

	if (slots > 2)
		return -EINVAL;

	rt1308->rx_mask = rx_mask;
	rt1308->slots = slots;
	/* slot_width is not used since it's irrelevant for SoundWire */

	return 0;
}

static int rt1308_sdw_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1308_sdw_priv *rt1308 =
		snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config = {0};
	struct sdw_port_config port_config = {0};
	struct sdw_stream_data *stream;
	int retval;

	dev_dbg(dai->dev, "%s %s", __func__, dai->name);
	stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!stream)
		return -EINVAL;

	if (!rt1308->sdw_slave)
		return -EINVAL;

	/* SoundWire specific configuration */
	snd_sdw_params_to_config(substream, params, &stream_config, &port_config);

	/* port 1 for playback */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		port_config.num = 1;
	else
		return -EINVAL;

	if (rt1308->slots) {
		stream_config.ch_count = rt1308->slots;
		port_config.ch_mask = rt1308->rx_mask;
	}

	retval = sdw_stream_add_slave(rt1308->sdw_slave, &stream_config,
				&port_config, 1, stream->sdw_stream);
	if (retval) {
		dev_err(dai->dev, "Unable to configure port\n");
		return retval;
	}

	return retval;
}

static int rt1308_sdw_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1308_sdw_priv *rt1308 =
		snd_soc_component_get_drvdata(component);
	struct sdw_stream_data *stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt1308->sdw_slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt1308->sdw_slave, stream->sdw_stream);
	return 0;
}

/*
 * slave_ops: callbacks for get_clock_stop_mode, clock_stop and
 * port_prep are not defined for now
 */
static const struct sdw_slave_ops rt1308_slave_ops = {
	.read_prop = rt1308_read_prop,
	.interrupt_callback = rt1308_interrupt_callback,
	.update_status = rt1308_update_status,
	.bus_config = rt1308_bus_config,
};

static int rt1308_sdw_parse_dt(struct rt1308_sdw_priv *rt1308, struct device *dev)
{
	int ret = 0;

	device_property_read_u32(dev, "realtek,bq-params-cnt", &rt1308->bq_params_cnt);
	if (rt1308->bq_params_cnt) {
		rt1308->bq_params = devm_kzalloc(dev, rt1308->bq_params_cnt, GFP_KERNEL);
		if (!rt1308->bq_params) {
			dev_err(dev, "Could not allocate bq_params memory\n");
			ret = -ENOMEM;
		} else {
			ret = device_property_read_u8_array(dev, "realtek,bq-params", rt1308->bq_params, rt1308->bq_params_cnt);
			if (ret < 0)
				dev_err(dev, "Could not read list of realtek,bq-params\n");
		}
	}

	dev_dbg(dev, "bq_params_cnt=%d\n", rt1308->bq_params_cnt);
	return ret;
}

static int rt1308_sdw_component_probe(struct snd_soc_component *component)
{
	struct rt1308_sdw_priv *rt1308 = snd_soc_component_get_drvdata(component);
	int ret;

	rt1308->component = component;
	rt1308_sdw_parse_dt(rt1308, &rt1308->sdw_slave->dev);

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	/* apply BQ params */
	rt1308_apply_bq_params(rt1308);

	return 0;
}

static const struct snd_soc_component_driver soc_component_sdw_rt1308 = {
	.probe = rt1308_sdw_component_probe,
	.controls = rt1308_snd_controls,
	.num_controls = ARRAY_SIZE(rt1308_snd_controls),
	.dapm_widgets = rt1308_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt1308_dapm_widgets),
	.dapm_routes = rt1308_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt1308_dapm_routes),
	.endianness = 1,
};

static const struct snd_soc_dai_ops rt1308_aif_dai_ops = {
	.hw_params = rt1308_sdw_hw_params,
	.hw_free	= rt1308_sdw_pcm_hw_free,
	.set_stream	= rt1308_set_sdw_stream,
	.shutdown	= rt1308_sdw_shutdown,
	.set_tdm_slot	= rt1308_sdw_set_tdm_slot,
};

#define RT1308_STEREO_RATES SNDRV_PCM_RATE_48000
#define RT1308_FORMATS (SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver rt1308_sdw_dai[] = {
	{
		.name = "rt1308-aif",
		.playback = {
			.stream_name = "DP1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT1308_STEREO_RATES,
			.formats = RT1308_FORMATS,
		},
		.ops = &rt1308_aif_dai_ops,
	},
};

static int rt1308_sdw_init(struct device *dev, struct regmap *regmap,
				struct sdw_slave *slave)
{
	struct rt1308_sdw_priv *rt1308;
	int ret;

	rt1308 = devm_kzalloc(dev, sizeof(*rt1308), GFP_KERNEL);
	if (!rt1308)
		return -ENOMEM;

	dev_set_drvdata(dev, rt1308);
	rt1308->sdw_slave = slave;
	rt1308->regmap = regmap;

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt1308->hw_init = false;
	rt1308->first_hw_init = false;

	ret =  devm_snd_soc_register_component(dev,
				&soc_component_sdw_rt1308,
				rt1308_sdw_dai,
				ARRAY_SIZE(rt1308_sdw_dai));

	dev_dbg(&slave->dev, "%s\n", __func__);

	return ret;
}

static int rt1308_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct regmap *regmap;

	/* Regmap Initialization */
	regmap = devm_regmap_init_sdw(slave, &rt1308_sdw_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	rt1308_sdw_init(&slave->dev, regmap, slave);

	return 0;
}

static int rt1308_sdw_remove(struct sdw_slave *slave)
{
	struct rt1308_sdw_priv *rt1308 = dev_get_drvdata(&slave->dev);

	if (rt1308->first_hw_init)
		pm_runtime_disable(&slave->dev);

	return 0;
}

static const struct sdw_device_id rt1308_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x1308, 0x2, 0, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt1308_id);

static int __maybe_unused rt1308_dev_suspend(struct device *dev)
{
	struct rt1308_sdw_priv *rt1308 = dev_get_drvdata(dev);

	if (!rt1308->hw_init)
		return 0;

	regcache_cache_only(rt1308->regmap, true);

	return 0;
}

#define RT1308_PROBE_TIMEOUT 5000

static int __maybe_unused rt1308_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt1308_sdw_priv *rt1308 = dev_get_drvdata(dev);
	unsigned long time;

	if (!rt1308->first_hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->initialization_complete,
				msecs_to_jiffies(RT1308_PROBE_TIMEOUT));
	if (!time) {
		dev_err(&slave->dev, "Initialization not complete, timed out\n");
		sdw_show_ping_status(slave->bus, true);

		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(rt1308->regmap, false);
	regcache_sync_region(rt1308->regmap, 0xc000, 0xcfff);

	return 0;
}

static const struct dev_pm_ops rt1308_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(rt1308_dev_suspend, rt1308_dev_resume)
	SET_RUNTIME_PM_OPS(rt1308_dev_suspend, rt1308_dev_resume, NULL)
};

static struct sdw_driver rt1308_sdw_driver = {
	.driver = {
		.name = "rt1308",
		.owner = THIS_MODULE,
		.pm = &rt1308_pm,
	},
	.probe = rt1308_sdw_probe,
	.remove = rt1308_sdw_remove,
	.ops = &rt1308_slave_ops,
	.id_table = rt1308_id,
};
module_sdw_driver(rt1308_sdw_driver);

MODULE_DESCRIPTION("ASoC RT1308 driver SDW");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL v2");
