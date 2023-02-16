// SPDX-License-Identifier: GPL-2.0-only
//
// rt5682-sdw.c  --  RT5682 ALSA SoC audio component driver
//
// Copyright 2019 Realtek Semiconductor Corp.
// Author: Oder Chiou <oder_chiou@realtek.com>
//

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/sdw.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rt5682.h"

#define RT5682_SDW_ADDR_L			0x3000
#define RT5682_SDW_ADDR_H			0x3001
#define RT5682_SDW_DATA_L			0x3004
#define RT5682_SDW_DATA_H			0x3005
#define RT5682_SDW_CMD				0x3008

static int rt5682_sdw_read(void *context, unsigned int reg, unsigned int *val)
{
	struct device *dev = context;
	struct rt5682_priv *rt5682 = dev_get_drvdata(dev);
	unsigned int data_l, data_h;

	regmap_write(rt5682->sdw_regmap, RT5682_SDW_CMD, 0);
	regmap_write(rt5682->sdw_regmap, RT5682_SDW_ADDR_H, (reg >> 8) & 0xff);
	regmap_write(rt5682->sdw_regmap, RT5682_SDW_ADDR_L, (reg & 0xff));
	regmap_read(rt5682->sdw_regmap, RT5682_SDW_DATA_H, &data_h);
	regmap_read(rt5682->sdw_regmap, RT5682_SDW_DATA_L, &data_l);

	*val = (data_h << 8) | data_l;

	dev_vdbg(dev, "[%s] %04x => %04x\n", __func__, reg, *val);

	return 0;
}

static int rt5682_sdw_write(void *context, unsigned int reg, unsigned int val)
{
	struct device *dev = context;
	struct rt5682_priv *rt5682 = dev_get_drvdata(dev);

	regmap_write(rt5682->sdw_regmap, RT5682_SDW_CMD, 1);
	regmap_write(rt5682->sdw_regmap, RT5682_SDW_ADDR_H, (reg >> 8) & 0xff);
	regmap_write(rt5682->sdw_regmap, RT5682_SDW_ADDR_L, (reg & 0xff));
	regmap_write(rt5682->sdw_regmap, RT5682_SDW_DATA_H, (val >> 8) & 0xff);
	regmap_write(rt5682->sdw_regmap, RT5682_SDW_DATA_L, (val & 0xff));

	dev_vdbg(dev, "[%s] %04x <= %04x\n", __func__, reg, val);

	return 0;
}

static const struct regmap_config rt5682_sdw_indirect_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.max_register = RT5682_I2C_MODE,
	.volatile_reg = rt5682_volatile_register,
	.readable_reg = rt5682_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5682_reg,
	.num_reg_defaults = RT5682_REG_NUM,
	.use_single_read = true,
	.use_single_write = true,
	.reg_read = rt5682_sdw_read,
	.reg_write = rt5682_sdw_write,
};

struct sdw_stream_data {
	struct sdw_stream_runtime *sdw_stream;
};

static int rt5682_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
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
	snd_soc_dai_dma_data_set(dai, direction, stream);

	return 0;
}

static void rt5682_sdw_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sdw_stream_data *stream;

	stream = snd_soc_dai_get_dma_data(dai, substream);
	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(stream);
}

static int rt5682_sdw_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config = {0};
	struct sdw_port_config port_config = {0};
	struct sdw_stream_data *stream;
	int retval;
	unsigned int val_p = 0, val_c = 0, osr_p = 0, osr_c = 0;

	dev_dbg(dai->dev, "%s %s", __func__, dai->name);

	stream = snd_soc_dai_get_dma_data(dai, substream);
	if (!stream)
		return -ENOMEM;

	if (!rt5682->slave)
		return -EINVAL;

	/* SoundWire specific configuration */
	snd_sdw_params_to_config(substream, params, &stream_config, &port_config);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		port_config.num = 1;
	else
		port_config.num = 2;

	retval = sdw_stream_add_slave(rt5682->slave, &stream_config,
				      &port_config, 1, stream->sdw_stream);
	if (retval) {
		dev_err(dai->dev, "Unable to configure port\n");
		return retval;
	}

	switch (params_rate(params)) {
	case 48000:
		val_p = RT5682_SDW_REF_1_48K;
		val_c = RT5682_SDW_REF_2_48K;
		break;
	case 96000:
		val_p = RT5682_SDW_REF_1_96K;
		val_c = RT5682_SDW_REF_2_96K;
		break;
	case 192000:
		val_p = RT5682_SDW_REF_1_192K;
		val_c = RT5682_SDW_REF_2_192K;
		break;
	case 32000:
		val_p = RT5682_SDW_REF_1_32K;
		val_c = RT5682_SDW_REF_2_32K;
		break;
	case 24000:
		val_p = RT5682_SDW_REF_1_24K;
		val_c = RT5682_SDW_REF_2_24K;
		break;
	case 16000:
		val_p = RT5682_SDW_REF_1_16K;
		val_c = RT5682_SDW_REF_2_16K;
		break;
	case 12000:
		val_p = RT5682_SDW_REF_1_12K;
		val_c = RT5682_SDW_REF_2_12K;
		break;
	case 8000:
		val_p = RT5682_SDW_REF_1_8K;
		val_c = RT5682_SDW_REF_2_8K;
		break;
	case 44100:
		val_p = RT5682_SDW_REF_1_44K;
		val_c = RT5682_SDW_REF_2_44K;
		break;
	case 88200:
		val_p = RT5682_SDW_REF_1_88K;
		val_c = RT5682_SDW_REF_2_88K;
		break;
	case 176400:
		val_p = RT5682_SDW_REF_1_176K;
		val_c = RT5682_SDW_REF_2_176K;
		break;
	case 22050:
		val_p = RT5682_SDW_REF_1_22K;
		val_c = RT5682_SDW_REF_2_22K;
		break;
	case 11025:
		val_p = RT5682_SDW_REF_1_11K;
		val_c = RT5682_SDW_REF_2_11K;
		break;
	default:
		return -EINVAL;
	}

	if (params_rate(params) <= 48000) {
		osr_p = RT5682_DAC_OSR_D_8;
		osr_c = RT5682_ADC_OSR_D_8;
	} else if (params_rate(params) <= 96000) {
		osr_p = RT5682_DAC_OSR_D_4;
		osr_c = RT5682_ADC_OSR_D_4;
	} else {
		osr_p = RT5682_DAC_OSR_D_2;
		osr_c = RT5682_ADC_OSR_D_2;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(rt5682->regmap, RT5682_SDW_REF_CLK,
			RT5682_SDW_REF_1_MASK, val_p);
		regmap_update_bits(rt5682->regmap, RT5682_ADDA_CLK_1,
			RT5682_DAC_OSR_MASK, osr_p);
	} else {
		regmap_update_bits(rt5682->regmap, RT5682_SDW_REF_CLK,
			RT5682_SDW_REF_2_MASK, val_c);
		regmap_update_bits(rt5682->regmap, RT5682_ADDA_CLK_1,
			RT5682_ADC_OSR_MASK, osr_c);
	}

	return retval;
}

static int rt5682_sdw_hw_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt5682_priv *rt5682 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_data *stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt5682->slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt5682->slave, stream->sdw_stream);
	return 0;
}

static const struct snd_soc_dai_ops rt5682_sdw_ops = {
	.hw_params	= rt5682_sdw_hw_params,
	.hw_free	= rt5682_sdw_hw_free,
	.set_stream	= rt5682_set_sdw_stream,
	.shutdown	= rt5682_sdw_shutdown,
};

static struct snd_soc_dai_driver rt5682_dai[] = {
	{
		.name = "rt5682-aif1",
		.id = RT5682_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682_STEREO_RATES,
			.formats = RT5682_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682_STEREO_RATES,
			.formats = RT5682_FORMATS,
		},
		.ops = &rt5682_aif1_dai_ops,
	},
	{
		.name = "rt5682-aif2",
		.id = RT5682_AIF2,
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682_STEREO_RATES,
			.formats = RT5682_FORMATS,
		},
		.ops = &rt5682_aif2_dai_ops,
	},
	{
		.name = "rt5682-sdw",
		.id = RT5682_SDW,
		.playback = {
			.stream_name = "SDW Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682_STEREO_RATES,
			.formats = RT5682_FORMATS,
		},
		.capture = {
			.stream_name = "SDW Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682_STEREO_RATES,
			.formats = RT5682_FORMATS,
		},
		.ops = &rt5682_sdw_ops,
	},
};

static int rt5682_sdw_init(struct device *dev, struct regmap *regmap,
			   struct sdw_slave *slave)
{
	struct rt5682_priv *rt5682;
	int ret;

	rt5682 = devm_kzalloc(dev, sizeof(*rt5682), GFP_KERNEL);
	if (!rt5682)
		return -ENOMEM;

	dev_set_drvdata(dev, rt5682);
	rt5682->slave = slave;
	rt5682->sdw_regmap = regmap;
	rt5682->is_sdw = true;

	mutex_init(&rt5682->disable_irq_lock);

	rt5682->regmap = devm_regmap_init(dev, NULL, dev,
					  &rt5682_sdw_indirect_regmap);
	if (IS_ERR(rt5682->regmap)) {
		ret = PTR_ERR(rt5682->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt5682->hw_init = false;
	rt5682->first_hw_init = false;

	mutex_init(&rt5682->calibrate_mutex);
	INIT_DELAYED_WORK(&rt5682->jack_detect_work,
		rt5682_jack_detect_handler);

	ret = devm_snd_soc_register_component(dev,
					      &rt5682_soc_component_dev,
					      rt5682_dai, ARRAY_SIZE(rt5682_dai));
	dev_dbg(&slave->dev, "%s\n", __func__);

	return ret;
}

static int rt5682_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt5682_priv *rt5682 = dev_get_drvdata(dev);
	int ret = 0, loop = 10;
	unsigned int val;

	rt5682->disable_irq = false;

	if (rt5682->hw_init)
		return 0;

	/*
	 * PM runtime is only enabled when a Slave reports as Attached
	 */
	if (!rt5682->first_hw_init) {
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

	if (rt5682->first_hw_init) {
		regcache_cache_only(rt5682->regmap, false);
		regcache_cache_bypass(rt5682->regmap, true);
	}

	while (loop > 0) {
		regmap_read(rt5682->regmap, RT5682_DEVICE_ID, &val);
		if (val == DEVICE_ID)
			break;
		dev_warn(dev, "Device with ID register %x is not rt5682\n", val);
		usleep_range(30000, 30005);
		loop--;
	}

	if (val != DEVICE_ID) {
		dev_err(dev, "Device with ID register %x is not rt5682\n", val);
		ret = -ENODEV;
		goto err_nodev;
	}

	rt5682_calibrate(rt5682);

	if (rt5682->first_hw_init) {
		regcache_cache_bypass(rt5682->regmap, false);
		regcache_mark_dirty(rt5682->regmap);
		regcache_sync(rt5682->regmap);

		/* volatile registers */
		regmap_update_bits(rt5682->regmap, RT5682_CBJ_CTRL_2,
			RT5682_EXT_JD_SRC, RT5682_EXT_JD_SRC_MANUAL);

		goto reinit;
	}

	rt5682_apply_patch_list(rt5682, dev);

	regmap_write(rt5682->regmap, RT5682_DEPOP_1, 0x0000);

	regmap_update_bits(rt5682->regmap, RT5682_PWR_ANLG_1,
		RT5682_LDO1_DVO_MASK | RT5682_HP_DRIVER_MASK,
		RT5682_LDO1_DVO_12 | RT5682_HP_DRIVER_5X);
	regmap_write(rt5682->regmap, RT5682_MICBIAS_2, 0x0080);
	regmap_write(rt5682->regmap, RT5682_TEST_MODE_CTRL_1, 0x0000);
	regmap_update_bits(rt5682->regmap, RT5682_BIAS_CUR_CTRL_8,
		RT5682_HPA_CP_BIAS_CTRL_MASK, RT5682_HPA_CP_BIAS_3UA);
	regmap_update_bits(rt5682->regmap, RT5682_CHARGE_PUMP_1,
		RT5682_CP_CLK_HP_MASK, RT5682_CP_CLK_HP_300KHZ);
	regmap_update_bits(rt5682->regmap, RT5682_HP_CHARGE_PUMP_1,
		RT5682_PM_HP_MASK, RT5682_PM_HP_HV);

	/* Soundwire */
	regmap_write(rt5682->regmap, RT5682_PLL2_INTERNAL, 0xa266);
	regmap_write(rt5682->regmap, RT5682_PLL2_CTRL_1, 0x1700);
	regmap_write(rt5682->regmap, RT5682_PLL2_CTRL_2, 0x0006);
	regmap_write(rt5682->regmap, RT5682_PLL2_CTRL_3, 0x2600);
	regmap_write(rt5682->regmap, RT5682_PLL2_CTRL_4, 0x0c8f);
	regmap_write(rt5682->regmap, RT5682_PLL_TRACK_2, 0x3000);
	regmap_write(rt5682->regmap, RT5682_PLL_TRACK_3, 0x4000);
	regmap_update_bits(rt5682->regmap, RT5682_GLB_CLK,
		RT5682_SCLK_SRC_MASK | RT5682_PLL2_SRC_MASK,
		RT5682_SCLK_SRC_PLL2 | RT5682_PLL2_SRC_SDW);

	regmap_update_bits(rt5682->regmap, RT5682_CBJ_CTRL_2,
		RT5682_EXT_JD_SRC, RT5682_EXT_JD_SRC_MANUAL);
	regmap_write(rt5682->regmap, RT5682_CBJ_CTRL_1, 0xd142);
	regmap_update_bits(rt5682->regmap, RT5682_CBJ_CTRL_5, 0x0700, 0x0600);
	regmap_update_bits(rt5682->regmap, RT5682_CBJ_CTRL_3,
		RT5682_CBJ_IN_BUF_EN, RT5682_CBJ_IN_BUF_EN);
	regmap_update_bits(rt5682->regmap, RT5682_SAR_IL_CMD_1,
		RT5682_SAR_POW_MASK, RT5682_SAR_POW_EN);
	regmap_update_bits(rt5682->regmap, RT5682_RC_CLK_CTRL,
		RT5682_POW_IRQ | RT5682_POW_JDH |
		RT5682_POW_ANA, RT5682_POW_IRQ |
		RT5682_POW_JDH | RT5682_POW_ANA);
	regmap_update_bits(rt5682->regmap, RT5682_PWR_ANLG_2,
		RT5682_PWR_JDH, RT5682_PWR_JDH);
	regmap_update_bits(rt5682->regmap, RT5682_IRQ_CTRL_2,
		RT5682_JD1_EN_MASK | RT5682_JD1_IRQ_MASK,
		RT5682_JD1_EN | RT5682_JD1_IRQ_PUL);

reinit:
	mod_delayed_work(system_power_efficient_wq,
		&rt5682->jack_detect_work, msecs_to_jiffies(250));

	/* Mark Slave initialization complete */
	rt5682->hw_init = true;
	rt5682->first_hw_init = true;

err_nodev:
	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	dev_dbg(&slave->dev, "%s hw_init complete: %d\n", __func__, ret);

	return ret;
}

static bool rt5682_sdw_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00e0:
	case 0x00f0:
	case 0x3000:
	case 0x3001:
	case 0x3004:
	case 0x3005:
	case 0x3008:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rt5682_sdw_regmap = {
	.name = "sdw",
	.reg_bits = 32,
	.val_bits = 8,
	.max_register = RT5682_I2C_MODE,
	.readable_reg = rt5682_sdw_readable_register,
	.cache_type = REGCACHE_NONE,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt5682_update_status(struct sdw_slave *slave,
					enum sdw_slave_status status)
{
	struct rt5682_priv *rt5682 = dev_get_drvdata(&slave->dev);

	/* Update the status */
	rt5682->status = status;

	if (status == SDW_SLAVE_UNATTACHED)
		rt5682->hw_init = false;

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt5682->hw_init || rt5682->status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt5682_io_init(&slave->dev, slave);
}

static int rt5682_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval, i;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->scp_int1_mask = SDW_SCP_INT1_IMPL_DEF | SDW_SCP_INT1_BUS_CLASH |
		SDW_SCP_INT1_PARITY;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;

	prop->paging_support = false;

	/* first we need to allocate memory for set bits in port lists */
	prop->source_ports = 0x4;	/* BITMAP: 00000100 */
	prop->sink_ports = 0x2;		/* BITMAP: 00000010 */

	nval = hweight32(prop->source_ports);
	prop->src_dpn_prop = devm_kcalloc(&slave->dev, nval,
					  sizeof(*prop->src_dpn_prop),
					  GFP_KERNEL);
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

	/* wake-up event */
	prop->wake_capable = 1;

	return 0;
}

/* Bus clock frequency */
#define RT5682_CLK_FREQ_9600000HZ 9600000
#define RT5682_CLK_FREQ_12000000HZ 12000000
#define RT5682_CLK_FREQ_6000000HZ 6000000
#define RT5682_CLK_FREQ_4800000HZ 4800000
#define RT5682_CLK_FREQ_2400000HZ 2400000
#define RT5682_CLK_FREQ_12288000HZ 12288000

static int rt5682_clock_config(struct device *dev)
{
	struct rt5682_priv *rt5682 = dev_get_drvdata(dev);
	unsigned int clk_freq, value;

	clk_freq = (rt5682->params.curr_dr_freq >> 1);

	switch (clk_freq) {
	case RT5682_CLK_FREQ_12000000HZ:
		value = 0x0;
		break;
	case RT5682_CLK_FREQ_6000000HZ:
		value = 0x1;
		break;
	case RT5682_CLK_FREQ_9600000HZ:
		value = 0x2;
		break;
	case RT5682_CLK_FREQ_4800000HZ:
		value = 0x3;
		break;
	case RT5682_CLK_FREQ_2400000HZ:
		value = 0x4;
		break;
	case RT5682_CLK_FREQ_12288000HZ:
		value = 0x5;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(rt5682->sdw_regmap, 0xe0, value);
	regmap_write(rt5682->sdw_regmap, 0xf0, value);

	dev_dbg(dev, "%s complete, clk_freq=%d\n", __func__, clk_freq);

	return 0;
}

static int rt5682_bus_config(struct sdw_slave *slave,
					struct sdw_bus_params *params)
{
	struct rt5682_priv *rt5682 = dev_get_drvdata(&slave->dev);
	int ret;

	memcpy(&rt5682->params, params, sizeof(*params));

	ret = rt5682_clock_config(&slave->dev);
	if (ret < 0)
		dev_err(&slave->dev, "Invalid clk config");

	return ret;
}

static int rt5682_interrupt_callback(struct sdw_slave *slave,
					struct sdw_slave_intr_status *status)
{
	struct rt5682_priv *rt5682 = dev_get_drvdata(&slave->dev);

	dev_dbg(&slave->dev,
		"%s control_port_stat=%x", __func__, status->control_port);

	mutex_lock(&rt5682->disable_irq_lock);
	if (status->control_port & 0x4 && !rt5682->disable_irq) {
		mod_delayed_work(system_power_efficient_wq,
			&rt5682->jack_detect_work, msecs_to_jiffies(rt5682->irq_work_delay_time));
	}
	mutex_unlock(&rt5682->disable_irq_lock);

	return 0;
}

static const struct sdw_slave_ops rt5682_slave_ops = {
	.read_prop = rt5682_read_prop,
	.interrupt_callback = rt5682_interrupt_callback,
	.update_status = rt5682_update_status,
	.bus_config = rt5682_bus_config,
};

static int rt5682_sdw_probe(struct sdw_slave *slave,
			   const struct sdw_device_id *id)
{
	struct regmap *regmap;

	/* Regmap Initialization */
	regmap = devm_regmap_init_sdw(slave, &rt5682_sdw_regmap);
	if (IS_ERR(regmap))
		return -EINVAL;

	rt5682_sdw_init(&slave->dev, regmap, slave);

	return 0;
}

static int rt5682_sdw_remove(struct sdw_slave *slave)
{
	struct rt5682_priv *rt5682 = dev_get_drvdata(&slave->dev);

	if (rt5682->hw_init)
		cancel_delayed_work_sync(&rt5682->jack_detect_work);

	if (rt5682->first_hw_init)
		pm_runtime_disable(&slave->dev);

	return 0;
}

static const struct sdw_device_id rt5682_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x5682, 0x2, 0, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt5682_id);

static int __maybe_unused rt5682_dev_suspend(struct device *dev)
{
	struct rt5682_priv *rt5682 = dev_get_drvdata(dev);

	if (!rt5682->hw_init)
		return 0;

	cancel_delayed_work_sync(&rt5682->jack_detect_work);

	regcache_cache_only(rt5682->regmap, true);
	regcache_mark_dirty(rt5682->regmap);

	return 0;
}

static int __maybe_unused rt5682_dev_system_suspend(struct device *dev)
{
	struct rt5682_priv *rt5682 = dev_get_drvdata(dev);
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	int ret;

	if (!rt5682->hw_init)
		return 0;

	/*
	 * prevent new interrupts from being handled after the
	 * deferred work completes and before the parent disables
	 * interrupts on the link
	 */
	mutex_lock(&rt5682->disable_irq_lock);
	rt5682->disable_irq = true;
	ret = sdw_update_no_pm(slave, SDW_SCP_INTMASK1,
			       SDW_SCP_INT1_IMPL_DEF, 0);
	mutex_unlock(&rt5682->disable_irq_lock);

	if (ret < 0) {
		/* log but don't prevent suspend from happening */
		dev_dbg(&slave->dev, "%s: could not disable imp-def interrupts\n:", __func__);
	}

	return rt5682_dev_suspend(dev);
}

static int __maybe_unused rt5682_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt5682_priv *rt5682 = dev_get_drvdata(dev);
	unsigned long time;

	if (!rt5682->first_hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->initialization_complete,
				msecs_to_jiffies(RT5682_PROBE_TIMEOUT));
	if (!time) {
		dev_err(&slave->dev, "Initialization not complete, timed out\n");
		sdw_show_ping_status(slave->bus, true);

		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(rt5682->regmap, false);
	regcache_sync(rt5682->regmap);

	return 0;
}

static const struct dev_pm_ops rt5682_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(rt5682_dev_system_suspend, rt5682_dev_resume)
	SET_RUNTIME_PM_OPS(rt5682_dev_suspend, rt5682_dev_resume, NULL)
};

static struct sdw_driver rt5682_sdw_driver = {
	.driver = {
		.name = "rt5682",
		.owner = THIS_MODULE,
		.pm = &rt5682_pm,
	},
	.probe = rt5682_sdw_probe,
	.remove = rt5682_sdw_remove,
	.ops = &rt5682_slave_ops,
	.id_table = rt5682_id,
};
module_sdw_driver(rt5682_sdw_driver);

MODULE_DESCRIPTION("ASoC RT5682 driver SDW");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
