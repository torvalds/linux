// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/auxiliary_bus.h>
#include <linux/cleanup.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/pcm.h>
#include <sound/sdca_asoc.h>
#include <sound/sdca_fdl.h>
#include <sound/sdca_function.h>
#include <sound/sdca_interrupts.h>
#include <sound/sdca_jack.h>
#include <sound/sdca_regmap.h>
#include <sound/sdw.h>
#include <sound/soc-component.h>
#include <sound/soc-dai.h>
#include <sound/soc.h>
#include "sdca_class.h"

struct class_function_drv {
	struct device *dev;
	struct regmap *regmap;
	struct sdca_class_drv *core;

	struct sdca_function_data *function;
	bool suspended;
};

static void class_function_regmap_lock(void *data)
{
	struct mutex *lock = data;

	mutex_lock(lock);
}

static void class_function_regmap_unlock(void *data)
{
	struct mutex *lock = data;

	mutex_unlock(lock);
}

static bool class_function_regmap_writeable(struct device *dev, unsigned int reg)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct class_function_drv *drv = auxiliary_get_drvdata(auxdev);

	return sdca_regmap_writeable(drv->function, reg);
}

static bool class_function_regmap_readable(struct device *dev, unsigned int reg)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct class_function_drv *drv = auxiliary_get_drvdata(auxdev);

	return sdca_regmap_readable(drv->function, reg);
}

static bool class_function_regmap_volatile(struct device *dev, unsigned int reg)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct class_function_drv *drv = auxiliary_get_drvdata(auxdev);

	return sdca_regmap_volatile(drv->function, reg);
}

static const struct regmap_config class_function_regmap_config = {
	.name			= "sdca",
	.reg_bits		= 32,
	.val_bits		= 32,
	.reg_format_endian	= REGMAP_ENDIAN_LITTLE,
	.val_format_endian	= REGMAP_ENDIAN_LITTLE,

	.max_register		= SDW_SDCA_MAX_REGISTER,
	.readable_reg		= class_function_regmap_readable,
	.writeable_reg		= class_function_regmap_writeable,
	.volatile_reg		= class_function_regmap_volatile,

	.cache_type		= REGCACHE_MAPLE,

	.lock			= class_function_regmap_lock,
	.unlock			= class_function_regmap_unlock,
};

static int class_function_regmap_mbq_size(struct device *dev, unsigned int reg)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct class_function_drv *drv = auxiliary_get_drvdata(auxdev);

	return sdca_regmap_mbq_size(drv->function, reg);
}

static bool class_function_regmap_deferrable(struct device *dev, unsigned int reg)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct class_function_drv *drv = auxiliary_get_drvdata(auxdev);

	return sdca_regmap_deferrable(drv->function, reg);
}

static const struct regmap_sdw_mbq_cfg class_function_mbq_config = {
	.mbq_size		= class_function_regmap_mbq_size,
	.deferrable		= class_function_regmap_deferrable,
	.retry_us		= 1000,
	.timeout_us		= 10000,
};

static int class_function_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct class_function_drv *drv = snd_soc_component_get_drvdata(dai->component);

	return sdca_asoc_set_constraints(drv->dev, drv->regmap, drv->function,
					 substream, dai);
}

static int class_function_sdw_add_peripheral(struct snd_pcm_substream *substream,
					     struct snd_pcm_hw_params *params,
					     struct snd_soc_dai *dai)
{
	struct class_function_drv *drv = snd_soc_component_get_drvdata(dai->component);
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	struct sdw_slave *sdw = dev_to_sdw_dev(drv->dev->parent);
	struct sdw_stream_config sconfig = {0};
	struct sdw_port_config pconfig = {0};
	int ret;

	if (!sdw_stream)
		return -EINVAL;

	snd_sdw_params_to_config(substream, params, &sconfig, &pconfig);

	/*
	 * FIXME: As also noted in sdca_asoc_get_port(), currently only
	 * a single unshared port is supported for each DAI.
	 */
	ret = sdca_asoc_get_port(drv->dev, drv->regmap, drv->function, dai);
	if (ret < 0)
		return ret;

	pconfig.num = ret;

	ret = sdw_stream_add_slave(sdw, &sconfig, &pconfig, 1, sdw_stream);
	if (ret) {
		dev_err(drv->dev, "failed to add sdw stream: %d\n", ret);
		return ret;
	}

	return sdca_asoc_hw_params(drv->dev, drv->regmap, drv->function,
				   substream, params, dai);
}

static int class_function_sdw_remove_peripheral(struct snd_pcm_substream *substream,
						struct snd_soc_dai *dai)
{
	struct class_function_drv *drv = snd_soc_component_get_drvdata(dai->component);
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	struct sdw_slave *sdw = dev_to_sdw_dev(drv->dev->parent);

	if (!sdw_stream)
		return -EINVAL;

	return sdw_stream_remove_slave(sdw, sdw_stream);
}

static int class_function_sdw_set_stream(struct snd_soc_dai *dai, void *sdw_stream,
					 int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static const struct snd_soc_dai_ops class_function_sdw_ops = {
	.startup	= class_function_startup,
	.shutdown	= sdca_asoc_free_constraints,
	.set_stream	= class_function_sdw_set_stream,
	.hw_params	= class_function_sdw_add_peripheral,
	.hw_free	= class_function_sdw_remove_peripheral,
};

static int class_function_component_probe(struct snd_soc_component *component)
{
	struct class_function_drv *drv = snd_soc_component_get_drvdata(component);
	struct sdca_class_drv *core = drv->core;

	return sdca_irq_populate(drv->function, component, core->irq_info);
}

static int class_function_set_jack(struct snd_soc_component *component,
				   struct snd_soc_jack *jack, void *d)
{
	struct class_function_drv *drv = snd_soc_component_get_drvdata(component);
	struct sdca_class_drv *core = drv->core;

	return sdca_jack_set_jack(core->irq_info, jack);
}

static const struct snd_soc_component_driver class_function_component_drv = {
	.probe			= class_function_component_probe,
	.endianness		= 1,
};

static int class_function_init_device(struct class_function_drv *drv,
				      unsigned int status)
{
	int ret;

	if (!(status & SDCA_CTL_ENTITY_0_FUNCTION_HAS_BEEN_RESET)) {
		dev_dbg(drv->dev, "reset function device\n");

		ret = sdca_reset_function(drv->dev, drv->function, drv->regmap);
		if (ret)
			return ret;
	}

	if (status & SDCA_CTL_ENTITY_0_FUNCTION_NEEDS_INITIALIZATION) {
		dev_dbg(drv->dev, "write initialisation\n");

		ret = sdca_regmap_write_init(drv->dev, drv->core->dev_regmap,
					     drv->function);
		if (ret)
			return ret;
	}

	return 0;
}

static int class_function_boot(struct class_function_drv *drv)
{
	unsigned int reg = SDW_SDCA_CTL(drv->function->desc->adr,
					SDCA_ENTITY_TYPE_ENTITY_0,
					SDCA_CTL_ENTITY_0_FUNCTION_STATUS, 0);
	unsigned int val;
	int ret;

	guard(mutex)(&drv->core->init_lock);

	ret = regmap_read(drv->regmap, reg, &val);
	if (ret < 0) {
		dev_err(drv->dev, "failed to read function status: %d\n", ret);
		return ret;
	}

	ret = class_function_init_device(drv, val);
	if (ret)
		return ret;

	/* Start FDL process */
	ret = sdca_irq_populate_early(drv->dev, drv->regmap, drv->function,
				      drv->core->irq_info);
	if (ret)
		return ret;

	ret = sdca_fdl_sync(drv->dev, drv->function, drv->core->irq_info);
	if (ret)
		return ret;

	ret = sdca_regmap_write_defaults(drv->dev, drv->regmap, drv->function);
	if (ret)
		return ret;

	ret = regmap_write(drv->regmap, reg, 0xFF);
	if (ret < 0) {
		dev_err(drv->dev, "failed to clear function status: %d\n", ret);
		return ret;
	}

	return 0;
}

static int class_function_probe(struct auxiliary_device *auxdev,
				const struct auxiliary_device_id *aux_dev_id)
{
	struct device *dev = &auxdev->dev;
	struct sdca_class_drv *core = dev_get_drvdata(dev->parent);
	struct sdca_device_data *data = &core->sdw->sdca_data;
	struct sdca_function_desc *desc;
	struct snd_soc_component_driver *cmp_drv;
	struct snd_soc_dai_driver *dais;
	struct class_function_drv *drv;
	struct regmap_sdw_mbq_cfg *mbq_config;
	struct regmap_config *config;
	struct reg_default *defaults;
	int ndefaults;
	int num_dais;
	int ret;
	int i;

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	cmp_drv = devm_kmemdup(dev, &class_function_component_drv, sizeof(*cmp_drv),
			       GFP_KERNEL);
	if (!cmp_drv)
		return -ENOMEM;

	config = devm_kmemdup(dev, &class_function_regmap_config, sizeof(*config),
			      GFP_KERNEL);
	if (!config)
		return -ENOMEM;

	mbq_config = devm_kmemdup(dev, &class_function_mbq_config, sizeof(*mbq_config),
				  GFP_KERNEL);
	if (!mbq_config)
		return -ENOMEM;

	drv->dev = dev;
	drv->core = core;

	for (i = 0; i < data->num_functions; i++) {
		desc = &data->function[i];

		if (desc->type == aux_dev_id->driver_data)
			break;
	}
	if (i == core->sdw->sdca_data.num_functions) {
		dev_err(dev, "failed to locate function\n");
		return -EINVAL;
	}

	drv->function = &core->functions[i];

	ret = sdca_parse_function(dev, core->sdw, desc, drv->function);
	if (ret)
		return ret;

	ndefaults = sdca_regmap_count_constants(dev, drv->function);
	if (ndefaults < 0)
		return ndefaults;

	defaults = devm_kcalloc(dev, ndefaults, sizeof(*defaults), GFP_KERNEL);
	if (!defaults)
		return -ENOMEM;

	ret = sdca_regmap_populate_constants(dev, drv->function, defaults);
	if (ret < 0)
		return ret;

	regcache_sort_defaults(defaults, ndefaults);

	auxiliary_set_drvdata(auxdev, drv);

	config->reg_defaults = defaults;
	config->num_reg_defaults = ndefaults;
	config->lock_arg = &core->regmap_lock;

	if (drv->function->busy_max_delay) {
		mbq_config->timeout_us = drv->function->busy_max_delay;
		mbq_config->retry_us = umax(drv->function->busy_max_delay / 10,
					    mbq_config->retry_us);
	}

	drv->regmap = devm_regmap_init_sdw_mbq_cfg(dev, core->sdw, config, mbq_config);
	if (IS_ERR(drv->regmap))
		return dev_err_probe(dev, PTR_ERR(drv->regmap),
				     "failed to create regmap");

	if (desc->type == SDCA_FUNCTION_TYPE_UAJ)
		cmp_drv->set_jack = class_function_set_jack;

	ret = sdca_asoc_populate_component(dev, drv->function, cmp_drv,
					   &dais, &num_dais,
					   &class_function_sdw_ops);
	if (ret)
		return ret;

	dev_pm_set_driver_flags(dev, DPM_FLAG_NO_DIRECT_COMPLETE);

	pm_runtime_set_autosuspend_delay(dev, 200);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = class_function_boot(drv);
	if (ret)
		return ret;

	ret = devm_snd_soc_register_component(dev, cmp_drv, dais, num_dais);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register component\n");

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int class_function_runtime_suspend(struct device *dev)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct class_function_drv *drv = auxiliary_get_drvdata(auxdev);

	/*
	 * Whilst the driver doesn't power the chip down here, going into
	 * runtime suspend means the driver can't be sure the bus won't
	 * power down which would prevent communication with the device.
	 */
	regcache_cache_only(drv->regmap, true);

	return 0;
}

static int class_function_runtime_resume(struct device *dev)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct class_function_drv *drv = auxiliary_get_drvdata(auxdev);
	int ret;

	guard(mutex)(&drv->core->init_lock);

	regcache_mark_dirty(drv->regmap);
	regcache_cache_only(drv->regmap, false);

	if (drv->suspended) {
		unsigned int reg = SDW_SDCA_CTL(drv->function->desc->adr,
						SDCA_ENTITY_TYPE_ENTITY_0,
						SDCA_CTL_ENTITY_0_FUNCTION_STATUS, 0);
		unsigned int val;

		ret = regmap_read(drv->regmap, reg, &val);
		if (ret < 0) {
			dev_err(drv->dev, "failed to read function status: %d\n", ret);
			goto err;
		}

		ret = class_function_init_device(drv, val);
		if (ret)
			goto err;

		sdca_irq_enable_early(drv->function, drv->core->irq_info);

		ret = sdca_fdl_sync(drv->dev, drv->function, drv->core->irq_info);
		if (ret)
			goto err;

		sdca_irq_enable(drv->function, drv->core->irq_info);

		ret = regmap_write(drv->regmap, reg, 0xFF);
		if (ret < 0) {
			dev_err(drv->dev, "failed to clear function status: %d\n", ret);
			goto err;
		}

		drv->suspended = false;
	}

	ret = regcache_sync(drv->regmap);
	if (ret) {
		dev_err(drv->dev, "failed to restore register cache: %d\n", ret);
		goto err;
	}

	return 0;

err:
	regcache_cache_only(drv->regmap, true);

	return ret;
}

static int class_function_suspend(struct device *dev)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct class_function_drv *drv = auxiliary_get_drvdata(auxdev);
	int ret;

	drv->suspended = true;

	/* Ensure runtime resume runs on resume */
	ret = pm_runtime_resume_and_get(dev);
	if (ret) {
		dev_err(dev, "failed to resume for suspend: %d\n", ret);
		return ret;
	}

	sdca_irq_disable(drv->function, drv->core->irq_info);

	ret = pm_runtime_force_suspend(dev);
	if (ret) {
		dev_err(dev, "failed to force suspend: %d\n", ret);
		return ret;
	}

	pm_runtime_put_noidle(dev);

	return 0;
}

static int class_function_resume(struct device *dev)
{
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret) {
		dev_err(dev, "failed to force resume: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops class_function_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(class_function_suspend, class_function_resume)
	RUNTIME_PM_OPS(class_function_runtime_suspend,
		       class_function_runtime_resume, NULL)
};

static const struct auxiliary_device_id class_function_id_table[] = {
	{
		.name = "snd_soc_sdca." SDCA_FUNCTION_TYPE_SMART_AMP_NAME,
		.driver_data = SDCA_FUNCTION_TYPE_SMART_AMP,
	},
	{
		.name = "snd_soc_sdca." SDCA_FUNCTION_TYPE_SMART_MIC_NAME,
		.driver_data = SDCA_FUNCTION_TYPE_SMART_MIC,
	},
	{
		.name = "snd_soc_sdca." SDCA_FUNCTION_TYPE_UAJ_NAME,
		.driver_data = SDCA_FUNCTION_TYPE_UAJ,
	},
	{
		.name = "snd_soc_sdca." SDCA_FUNCTION_TYPE_HID_NAME,
		.driver_data = SDCA_FUNCTION_TYPE_HID,
	},
	{},
};
MODULE_DEVICE_TABLE(auxiliary, class_function_id_table);

static struct auxiliary_driver class_function_drv = {
	.driver = {
		.name		= "sdca_function",
		.pm		= pm_ptr(&class_function_pm_ops),
	},

	.probe		= class_function_probe,
	.id_table	= class_function_id_table
};
module_auxiliary_driver(class_function_drv);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SDCA Class Function Driver");
MODULE_IMPORT_NS("SND_SOC_SDCA");
