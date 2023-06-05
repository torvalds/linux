// SPDX-License-Identifier: GPL-2.0-only
//
// sdw-mockup.c -- a mockup SoundWire codec for tests where only the host
// drives the bus.
//
// Copyright(c) 2021 Intel Corporation
//
//

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/sdw.h>
#include <sound/soc.h>

struct  sdw_mockup_priv {
	struct sdw_slave *slave;
};

static int sdw_mockup_component_probe(struct snd_soc_component *component)
{
	return 0;
}

static void sdw_mockup_component_remove(struct snd_soc_component *component)
{
}

static const struct snd_soc_component_driver snd_soc_sdw_mockup_component = {
	.probe = sdw_mockup_component_probe,
	.remove = sdw_mockup_component_remove,
	.endianness = 1,
};

static int sdw_mockup_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				     int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void sdw_mockup_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int sdw_mockup_pcm_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct sdw_mockup_priv *sdw_mockup = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config = {0};
	struct sdw_port_config port_config = {0};
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	int ret;

	if (!sdw_stream)
		return -EINVAL;

	if (!sdw_mockup->slave)
		return -EINVAL;

	/* SoundWire specific configuration */
	snd_sdw_params_to_config(substream, params, &stream_config, &port_config);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		port_config.num = 1;
	else
		port_config.num = 8;

	ret = sdw_stream_add_slave(sdw_mockup->slave, &stream_config,
				   &port_config, 1, sdw_stream);
	if (ret)
		dev_err(dai->dev, "Unable to configure port\n");

	return ret;
}

static int sdw_mockup_pcm_hw_free(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct sdw_mockup_priv *sdw_mockup = snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!sdw_mockup->slave)
		return -EINVAL;

	sdw_stream_remove_slave(sdw_mockup->slave, sdw_stream);
	return 0;
}

static const struct snd_soc_dai_ops sdw_mockup_ops = {
	.hw_params	= sdw_mockup_pcm_hw_params,
	.hw_free	= sdw_mockup_pcm_hw_free,
	.set_stream	= sdw_mockup_set_sdw_stream,
	.shutdown	= sdw_mockup_shutdown,
};

static struct snd_soc_dai_driver sdw_mockup_dai[] = {
	{
		.name = "sdw-mockup-aif1",
		.id = 1,
		.playback = {
			.stream_name = "DP1 Playback",
			.channels_min = 1,
			.channels_max = 2,
		},
		.capture = {
			.stream_name = "DP8 Capture",
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &sdw_mockup_ops,
	},
};

static int sdw_mockup_update_status(struct sdw_slave *slave,
				    enum sdw_slave_status status)
{
	return 0;
}

static int sdw_mockup_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval;
	int i, j;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->paging_support = false;

	/*
	 * first we need to allocate memory for set bits in port lists
	 * the port allocation is completely arbitrary:
	 * DP0 is not supported
	 * DP1 is sink
	 * DP8 is source
	 */
	prop->source_ports = BIT(8);
	prop->sink_ports = BIT(1);

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
		i++;
	}

	/* do this again for sink now */
	nval = hweight32(prop->sink_ports);
	prop->sink_dpn_prop = devm_kcalloc(&slave->dev, nval,
					   sizeof(*prop->sink_dpn_prop),
					   GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;

	j = 0;
	dpn = prop->sink_dpn_prop;
	addr = prop->sink_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[j].num = bit;
		dpn[j].type = SDW_DPN_FULL;
		dpn[j].simple_ch_prep_sm = true;
		j++;
	}

	prop->simple_clk_stop_capable = true;

	/* wake-up event */
	prop->wake_capable = 0;

	return 0;
}

static int sdw_mockup_bus_config(struct sdw_slave *slave,
				 struct sdw_bus_params *params)
{
	return 0;
}

static int sdw_mockup_interrupt_callback(struct sdw_slave *slave,
					 struct sdw_slave_intr_status *status)
{
	return 0;
}

static const struct sdw_slave_ops sdw_mockup_slave_ops = {
	.read_prop = sdw_mockup_read_prop,
	.interrupt_callback = sdw_mockup_interrupt_callback,
	.update_status = sdw_mockup_update_status,
	.bus_config = sdw_mockup_bus_config,
};

static int sdw_mockup_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct device *dev = &slave->dev;
	struct sdw_mockup_priv *sdw_mockup;
	int ret;

	sdw_mockup = devm_kzalloc(dev, sizeof(*sdw_mockup), GFP_KERNEL);
	if (!sdw_mockup)
		return -ENOMEM;

	dev_set_drvdata(dev, sdw_mockup);
	sdw_mockup->slave = slave;

	slave->is_mockup_device = true;

	ret =  devm_snd_soc_register_component(dev,
					       &snd_soc_sdw_mockup_component,
					       sdw_mockup_dai,
					       ARRAY_SIZE(sdw_mockup_dai));

	return ret;
}

static int sdw_mockup_sdw_remove(struct sdw_slave *slave)
{
	return 0;
}

/*
 * Intel reserved parts ID with the following mapping expected:
 * 0xAAAA: generic full-duplex codec
 * 0xAA55: headset codec (mock-up of RT711/RT5682) - full-duplex
 * 0x55AA: amplifier (mock-up of RT1308/Maxim 98373) - playback only with
 * IV feedback
 * 0x5555: mic codec (mock-up of RT715) - capture-only
 */
static const struct sdw_device_id sdw_mockup_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x0105, 0xAAAA, 0x0, 0, 0),
	SDW_SLAVE_ENTRY_EXT(0x0105, 0xAA55, 0x0, 0, 0),
	SDW_SLAVE_ENTRY_EXT(0x0105, 0x55AA, 0x0, 0, 0),
	SDW_SLAVE_ENTRY_EXT(0x0105, 0x5555, 0x0, 0, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, sdw_mockup_id);

static struct sdw_driver sdw_mockup_sdw_driver = {
	.driver = {
		.name = "sdw-mockup",
		.owner = THIS_MODULE,
	},
	.probe = sdw_mockup_sdw_probe,
	.remove = sdw_mockup_sdw_remove,
	.ops = &sdw_mockup_slave_ops,
	.id_table = sdw_mockup_id,
};
module_sdw_driver(sdw_mockup_sdw_driver);

MODULE_DESCRIPTION("ASoC SDW mockup codec driver");
MODULE_AUTHOR("Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>");
MODULE_LICENSE("GPL");
