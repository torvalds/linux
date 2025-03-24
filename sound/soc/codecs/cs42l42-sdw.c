// SPDX-License-Identifier: GPL-2.0-only
// cs42l42-sdw.c -- CS42L42 ALSA SoC audio driver SoundWire driver
//
// Copyright (C) 2022 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/sdw.h>
#include <sound/soc.h>

#include "cs42l42.h"

#define CS42L42_SDW_CAPTURE_PORT	1
#define CS42L42_SDW_PLAYBACK_PORT	2

/* Register addresses are offset when sent over SoundWire */
#define CS42L42_SDW_ADDR_OFFSET		0x8000

#define CS42L42_SDW_MEM_ACCESS_STATUS	0xd0
#define CS42L42_SDW_MEM_READ_DATA	0xd8

#define CS42L42_SDW_LAST_LATE		BIT(3)
#define CS42L42_SDW_CMD_IN_PROGRESS	BIT(2)
#define CS42L42_SDW_RDATA_RDY		BIT(0)

#define CS42L42_DELAYED_READ_POLL_US	1
#define CS42L42_DELAYED_READ_TIMEOUT_US	100

static const struct snd_soc_dapm_route cs42l42_sdw_audio_map[] = {
	/* Playback Path */
	{ "HP", NULL, "MIXER" },
	{ "MIXER", NULL, "DACSRC" },
	{ "DACSRC", NULL, "Playback" },

	/* Capture Path */
	{ "ADCSRC", NULL, "HS" },
	{ "Capture", NULL, "ADCSRC" },
};

static int cs42l42_sdw_dai_startup(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(dai->component);

	if (!cs42l42->init_done)
		return -ENODEV;

	return 0;
}

static int cs42l42_sdw_dai_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(dai->component);
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	struct sdw_stream_config stream_config = {0};
	struct sdw_port_config port_config = {0};
	int ret;

	if (!sdw_stream)
		return -EINVAL;

	/* Needed for PLL configuration when we are notified of new bus config */
	cs42l42->sample_rate = params_rate(params);

	snd_sdw_params_to_config(substream, params, &stream_config, &port_config);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		port_config.num = CS42L42_SDW_PLAYBACK_PORT;
	else
		port_config.num = CS42L42_SDW_CAPTURE_PORT;

	ret = sdw_stream_add_slave(cs42l42->sdw_peripheral, &stream_config, &port_config, 1,
				   sdw_stream);
	if (ret) {
		dev_err(dai->dev, "Failed to add sdw stream: %d\n", ret);
		return ret;
	}

	cs42l42_src_config(dai->component, params_rate(params));

	return 0;
}

static int cs42l42_sdw_dai_prepare(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(dai->component);

	dev_dbg(dai->dev, "dai_prepare: sclk=%u rate=%u\n", cs42l42->sclk, cs42l42->sample_rate);

	if (!cs42l42->sclk || !cs42l42->sample_rate)
		return -EINVAL;

	/*
	 * At this point we know the sample rate from hw_params, and the SWIRE_CLK from bus_config()
	 * callback. This could only fail if the ACPI or machine driver are misconfigured to allow
	 * an unsupported SWIRE_CLK and sample_rate combination.
	 */

	return cs42l42_pll_config(dai->component, cs42l42->sclk, cs42l42->sample_rate);
}

static int cs42l42_sdw_dai_hw_free(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct cs42l42_private *cs42l42 = snd_soc_component_get_drvdata(dai->component);
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);

	sdw_stream_remove_slave(cs42l42->sdw_peripheral, sdw_stream);
	cs42l42->sample_rate = 0;

	return 0;
}

static int cs42l42_sdw_port_prep(struct sdw_slave *slave,
				 struct sdw_prepare_ch *prepare_ch,
				 enum sdw_port_prep_ops state)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(&slave->dev);
	unsigned int pdn_mask;

	if (prepare_ch->num == CS42L42_SDW_PLAYBACK_PORT)
		pdn_mask = CS42L42_HP_PDN_MASK;
	else
		pdn_mask = CS42L42_ADC_PDN_MASK;

	if (state == SDW_OPS_PORT_PRE_PREP) {
		dev_dbg(cs42l42->dev, "Prep Port pdn_mask:%x\n", pdn_mask);
		regmap_clear_bits(cs42l42->regmap, CS42L42_PWR_CTL1, pdn_mask);
		usleep_range(CS42L42_HP_ADC_EN_TIME_US, CS42L42_HP_ADC_EN_TIME_US + 1000);
	} else if (state == SDW_OPS_PORT_POST_DEPREP) {
		dev_dbg(cs42l42->dev, "Deprep Port pdn_mask:%x\n", pdn_mask);
		regmap_set_bits(cs42l42->regmap, CS42L42_PWR_CTL1, pdn_mask);
	}

	return 0;
}

static int cs42l42_sdw_dai_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
					  int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void cs42l42_sdw_dai_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static const struct snd_soc_dai_ops cs42l42_sdw_dai_ops = {
	.startup	= cs42l42_sdw_dai_startup,
	.shutdown	= cs42l42_sdw_dai_shutdown,
	.hw_params	= cs42l42_sdw_dai_hw_params,
	.prepare	= cs42l42_sdw_dai_prepare,
	.hw_free	= cs42l42_sdw_dai_hw_free,
	.mute_stream	= cs42l42_mute_stream,
	.set_stream	= cs42l42_sdw_dai_set_sdw_stream,
};

static struct snd_soc_dai_driver cs42l42_sdw_dai = {
	.name = "cs42l42-sdw",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		/* Restrict which rates and formats are supported */
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 1,
		/* Restrict which rates and formats are supported */
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
	.symmetric_rate = 1,
	.ops = &cs42l42_sdw_dai_ops,
};

static int cs42l42_sdw_poll_status(struct sdw_slave *peripheral, u8 mask, u8 match)
{
	int ret, sdwret;

	ret = read_poll_timeout(sdw_read_no_pm, sdwret,
				(sdwret < 0) || ((sdwret & mask) == match),
				CS42L42_DELAYED_READ_POLL_US, CS42L42_DELAYED_READ_TIMEOUT_US,
				false, peripheral, CS42L42_SDW_MEM_ACCESS_STATUS);
	if (ret == 0)
		ret = sdwret;

	if (ret < 0)
		dev_err(&peripheral->dev, "MEM_ACCESS_STATUS & %#x for %#x fail: %d\n",
			mask, match, ret);

	return ret;
}

static int cs42l42_sdw_read(void *context, unsigned int reg, unsigned int *val)
{
	struct sdw_slave *peripheral = context;
	u8 data;
	int ret;

	reg += CS42L42_SDW_ADDR_OFFSET;

	ret = cs42l42_sdw_poll_status(peripheral, CS42L42_SDW_CMD_IN_PROGRESS, 0);
	if (ret < 0)
		return ret;

	ret = sdw_read_no_pm(peripheral, reg);
	if (ret < 0) {
		dev_err(&peripheral->dev, "Failed to issue read @0x%x: %d\n", reg, ret);
		return ret;
	}

	data = (u8)ret;	/* possible non-delayed read value */
	ret = sdw_read_no_pm(peripheral, CS42L42_SDW_MEM_ACCESS_STATUS);
	if (ret < 0) {
		dev_err(&peripheral->dev, "Failed to read MEM_ACCESS_STATUS: %d\n", ret);
		return ret;
	}

	/* If read was not delayed we already have the result */
	if ((ret & CS42L42_SDW_LAST_LATE) == 0) {
		*val = data;
		return 0;
	}

	/* Poll for delayed read completion */
	if ((ret & CS42L42_SDW_RDATA_RDY) == 0) {
		ret = cs42l42_sdw_poll_status(peripheral,
					      CS42L42_SDW_RDATA_RDY, CS42L42_SDW_RDATA_RDY);
		if (ret < 0)
			return ret;
	}

	ret = sdw_read_no_pm(peripheral, CS42L42_SDW_MEM_READ_DATA);
	if (ret < 0) {
		dev_err(&peripheral->dev, "Failed to read READ_DATA: %d\n", ret);
		return ret;
	}

	*val = (u8)ret;

	return 0;
}

static int cs42l42_sdw_write(void *context, unsigned int reg, unsigned int val)
{
	struct sdw_slave *peripheral = context;
	int ret;

	ret = cs42l42_sdw_poll_status(peripheral, CS42L42_SDW_CMD_IN_PROGRESS, 0);
	if (ret < 0)
		return ret;

	return sdw_write_no_pm(peripheral, reg + CS42L42_SDW_ADDR_OFFSET, (u8)val);
}

/* Initialise cs42l42 using SoundWire - this is only called once, during initialisation */
static void cs42l42_sdw_init(struct sdw_slave *peripheral)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(&peripheral->dev);
	int ret;

	regcache_cache_only(cs42l42->regmap, false);

	ret = cs42l42_init(cs42l42);
	if (ret < 0) {
		regcache_cache_only(cs42l42->regmap, true);
		goto err;
	}

	/* Write out any cached changes that happened between probe and attach */
	ret = regcache_sync(cs42l42->regmap);
	if (ret < 0)
		dev_warn(cs42l42->dev, "Failed to sync cache: %d\n", ret);

	/* Disable internal logic that makes clock-stop conditional */
	regmap_clear_bits(cs42l42->regmap, CS42L42_PWR_CTL3, CS42L42_SW_CLK_STP_STAT_SEL_MASK);

err:
	/* This cancels the pm_runtime_get_noresume() call from cs42l42_sdw_probe(). */
	pm_runtime_put_autosuspend(cs42l42->dev);
}

static int cs42l42_sdw_read_prop(struct sdw_slave *peripheral)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(&peripheral->dev);
	struct sdw_slave_prop *prop = &peripheral->prop;
	struct sdw_dpn_prop *ports;

	ports = devm_kcalloc(cs42l42->dev, 2, sizeof(*ports), GFP_KERNEL);
	if (!ports)
		return -ENOMEM;

	prop->source_ports = BIT(CS42L42_SDW_CAPTURE_PORT);
	prop->sink_ports = BIT(CS42L42_SDW_PLAYBACK_PORT);
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;
	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;

	/* DP1 - capture */
	ports[0].num = CS42L42_SDW_CAPTURE_PORT;
	ports[0].type = SDW_DPN_FULL;
	ports[0].ch_prep_timeout = 10;
	prop->src_dpn_prop = &ports[0];

	/* DP2 - playback */
	ports[1].num = CS42L42_SDW_PLAYBACK_PORT;
	ports[1].type = SDW_DPN_FULL;
	ports[1].ch_prep_timeout = 10;
	prop->sink_dpn_prop = &ports[1];

	return 0;
}

static int cs42l42_sdw_update_status(struct sdw_slave *peripheral,
				     enum sdw_slave_status status)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(&peripheral->dev);

	switch (status) {
	case SDW_SLAVE_ATTACHED:
		dev_dbg(cs42l42->dev, "ATTACHED\n");

		/*
		 * The SoundWire core can report stale ATTACH notifications
		 * if we hard-reset CS42L42 in probe() but it had already been
		 * enumerated. Reject the ATTACH if we haven't yet seen an
		 * UNATTACH report for the device being in reset.
		 */
		if (cs42l42->sdw_waiting_first_unattach)
			break;

		/*
		 * Initialise codec, this only needs to be done once.
		 * When resuming from suspend, resume callback will handle re-init of codec,
		 * using regcache_sync().
		 */
		if (!cs42l42->init_done)
			cs42l42_sdw_init(peripheral);
		break;
	case SDW_SLAVE_UNATTACHED:
		dev_dbg(cs42l42->dev, "UNATTACHED\n");

		if (cs42l42->sdw_waiting_first_unattach) {
			/*
			 * SoundWire core has seen that CS42L42 is not on
			 * the bus so release RESET and wait for ATTACH.
			 */
			cs42l42->sdw_waiting_first_unattach = false;
			gpiod_set_value_cansleep(cs42l42->reset_gpio, 1);
		}

		break;
	default:
		break;
	}

	return 0;
}

static int cs42l42_sdw_bus_config(struct sdw_slave *peripheral,
				  struct sdw_bus_params *params)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(&peripheral->dev);
	unsigned int new_sclk = params->curr_dr_freq / 2;

	/* The cs42l42 cannot support a glitchless SWIRE_CLK change. */
	if ((new_sclk != cs42l42->sclk) && cs42l42->stream_use) {
		dev_warn(cs42l42->dev, "Rejected SCLK change while audio active\n");
		return -EBUSY;
	}

	cs42l42->sclk = new_sclk;

	dev_dbg(cs42l42->dev, "bus_config: sclk=%u c=%u r=%u\n",
		cs42l42->sclk, params->col, params->row);

	return 0;
}

static const struct sdw_slave_ops cs42l42_sdw_ops = {
/* No interrupt callback because only hardware INT is supported for Jack Detect in the CS42L42 */
	.read_prop = cs42l42_sdw_read_prop,
	.update_status = cs42l42_sdw_update_status,
	.bus_config = cs42l42_sdw_bus_config,
	.port_prep = cs42l42_sdw_port_prep,
};

static int cs42l42_sdw_runtime_suspend(struct device *dev)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(dev);

	dev_dbg(dev, "Runtime suspend\n");

	if (!cs42l42->init_done)
		return 0;

	/* The host controller could suspend, which would mean no register access */
	regcache_cache_only(cs42l42->regmap, true);

	return 0;
}

static const struct reg_sequence cs42l42_soft_reboot_seq[] = {
	REG_SEQ0(CS42L42_SOFT_RESET_REBOOT, 0x1e),
};

static int cs42l42_sdw_handle_unattach(struct cs42l42_private *cs42l42)
{
	struct sdw_slave *peripheral = cs42l42->sdw_peripheral;

	if (!peripheral->unattach_request)
		return 0;

	/* Cannot access registers until master re-attaches. */
	dev_dbg(&peripheral->dev, "Wait for initialization_complete\n");
	if (!wait_for_completion_timeout(&peripheral->initialization_complete,
					 msecs_to_jiffies(5000))) {
		dev_err(&peripheral->dev, "initialization_complete timed out\n");
		return -ETIMEDOUT;
	}

	peripheral->unattach_request = 0;

	/*
	 * After a bus reset there must be a reconfiguration reset to
	 * reinitialize the internal state of CS42L42.
	 */
	regmap_multi_reg_write_bypassed(cs42l42->regmap,
					cs42l42_soft_reboot_seq,
					ARRAY_SIZE(cs42l42_soft_reboot_seq));
	usleep_range(CS42L42_BOOT_TIME_US, CS42L42_BOOT_TIME_US * 2);
	regcache_mark_dirty(cs42l42->regmap);

	return 0;
}

static int cs42l42_sdw_runtime_resume(struct device *dev)
{
	static const unsigned int ts_dbnce_ms[] = { 0, 125, 250, 500, 750, 1000, 1250, 1500};
	struct cs42l42_private *cs42l42 = dev_get_drvdata(dev);
	unsigned int dbnce;
	int ret;

	dev_dbg(dev, "Runtime resume\n");

	if (!cs42l42->init_done)
		return 0;

	ret = cs42l42_sdw_handle_unattach(cs42l42);
	if (ret < 0) {
		return ret;
	} else if (ret > 0) {
		dbnce = max(cs42l42->ts_dbnc_rise, cs42l42->ts_dbnc_fall);

		if (dbnce > 0)
			msleep(ts_dbnce_ms[dbnce]);
	}

	regcache_cache_only(cs42l42->regmap, false);

	/* Sync LATCH_TO_VP first so the VP domain registers sync correctly */
	regcache_sync_region(cs42l42->regmap, CS42L42_MIC_DET_CTL1, CS42L42_MIC_DET_CTL1);
	regcache_sync(cs42l42->regmap);

	return 0;
}

static int cs42l42_sdw_resume(struct device *dev)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "System resume\n");

	/* Power-up so it can re-enumerate */
	ret = cs42l42_resume(dev);
	if (ret)
		return ret;

	/* Wait for re-attach */
	ret = cs42l42_sdw_handle_unattach(cs42l42);
	if (ret < 0)
		return ret;

	cs42l42_resume_restore(dev);

	return 0;
}

static int cs42l42_sdw_probe(struct sdw_slave *peripheral, const struct sdw_device_id *id)
{
	struct snd_soc_component_driver *component_drv;
	struct device *dev = &peripheral->dev;
	struct cs42l42_private *cs42l42;
	struct regmap_config *regmap_conf;
	struct regmap *regmap;
	int irq, ret;

	cs42l42 = devm_kzalloc(dev, sizeof(*cs42l42), GFP_KERNEL);
	if (!cs42l42)
		return -ENOMEM;

	if (has_acpi_companion(dev))
		irq = acpi_dev_gpio_irq_get(ACPI_COMPANION(dev), 0);
	else
		irq = of_irq_get(dev->of_node, 0);

	if (irq == -ENOENT)
		irq = 0;
	else if (irq < 0)
		return dev_err_probe(dev, irq, "Failed to get IRQ\n");

	regmap_conf = devm_kmemdup(dev, &cs42l42_regmap, sizeof(cs42l42_regmap), GFP_KERNEL);
	if (!regmap_conf)
		return -ENOMEM;
	regmap_conf->reg_bits = 16;
	regmap_conf->num_ranges = 0;
	regmap_conf->reg_read = cs42l42_sdw_read;
	regmap_conf->reg_write = cs42l42_sdw_write;

	regmap = devm_regmap_init(dev, NULL, peripheral, regmap_conf);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to allocate register map\n");

	/* Start in cache-only until device is enumerated */
	regcache_cache_only(regmap, true);

	component_drv = devm_kmemdup(dev,
				     &cs42l42_soc_component,
				     sizeof(cs42l42_soc_component),
				     GFP_KERNEL);
	if (!component_drv)
		return -ENOMEM;

	component_drv->dapm_routes = cs42l42_sdw_audio_map;
	component_drv->num_dapm_routes = ARRAY_SIZE(cs42l42_sdw_audio_map);

	cs42l42->dev = dev;
	cs42l42->regmap = regmap;
	cs42l42->sdw_peripheral = peripheral;
	cs42l42->irq = irq;
	cs42l42->devid = CS42L42_CHIP_ID;

	/*
	 * pm_runtime is needed to control bus manager suspend, and to
	 * recover from an unattach_request when the manager suspends.
	 */
	pm_runtime_set_autosuspend_delay(cs42l42->dev, 3000);
	pm_runtime_use_autosuspend(cs42l42->dev);
	pm_runtime_mark_last_busy(cs42l42->dev);
	pm_runtime_set_active(cs42l42->dev);
	pm_runtime_get_noresume(cs42l42->dev);
	pm_runtime_enable(cs42l42->dev);

	ret = cs42l42_common_probe(cs42l42, component_drv, &cs42l42_sdw_dai);
	if (ret < 0)
		return ret;

	return 0;
}

static int cs42l42_sdw_remove(struct sdw_slave *peripheral)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(&peripheral->dev);

	cs42l42_common_remove(cs42l42);
	pm_runtime_disable(cs42l42->dev);

	return 0;
}

static const struct dev_pm_ops cs42l42_sdw_pm = {
	SYSTEM_SLEEP_PM_OPS(cs42l42_suspend, cs42l42_sdw_resume)
	RUNTIME_PM_OPS(cs42l42_sdw_runtime_suspend, cs42l42_sdw_runtime_resume, NULL)
};

static const struct sdw_device_id cs42l42_sdw_id[] = {
	SDW_SLAVE_ENTRY(0x01FA, 0x4242, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, cs42l42_sdw_id);

static struct sdw_driver cs42l42_sdw_driver = {
	.driver = {
		.name = "cs42l42-sdw",
		.pm = pm_ptr(&cs42l42_sdw_pm),
	},
	.probe = cs42l42_sdw_probe,
	.remove = cs42l42_sdw_remove,
	.ops = &cs42l42_sdw_ops,
	.id_table = cs42l42_sdw_id,
};

module_sdw_driver(cs42l42_sdw_driver);

MODULE_DESCRIPTION("ASoC CS42L42 SoundWire driver");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("SND_SOC_CS42L42_CORE");
