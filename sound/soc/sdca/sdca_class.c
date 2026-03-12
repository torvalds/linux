// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>
#include <sound/sdca_interrupts.h>
#include <sound/sdca_regmap.h>
#include "sdca_class.h"

#define CLASS_SDW_ATTACH_TIMEOUT_MS	5000

static int class_read_prop(struct sdw_slave *sdw)
{
	struct sdw_slave_prop *prop = &sdw->prop;

	sdw_slave_read_prop(sdw);

	prop->use_domain_irq = true;
	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY |
			      SDW_SCP_INT1_IMPL_DEF;

	return 0;
}

static int class_sdw_update_status(struct sdw_slave *sdw, enum sdw_slave_status status)
{
	struct sdca_class_drv *drv = dev_get_drvdata(&sdw->dev);

	switch (status) {
	case SDW_SLAVE_ATTACHED:
		dev_dbg(drv->dev, "device attach\n");

		drv->attached = true;

		complete(&drv->device_attach);
		break;
	case SDW_SLAVE_UNATTACHED:
		dev_dbg(drv->dev, "device detach\n");

		drv->attached = false;

		reinit_completion(&drv->device_attach);
		break;
	default:
		break;
	}

	return 0;
}

static const struct sdw_slave_ops class_sdw_ops = {
	.read_prop	= class_read_prop,
	.update_status	= class_sdw_update_status,
};

static void class_regmap_lock(void *data)
{
	struct mutex *lock = data;

	mutex_lock(lock);
}

static void class_regmap_unlock(void *data)
{
	struct mutex *lock = data;

	mutex_unlock(lock);
}

static int class_wait_for_attach(struct sdca_class_drv *drv)
{
	if (!drv->attached) {
		unsigned long timeout = msecs_to_jiffies(CLASS_SDW_ATTACH_TIMEOUT_MS);
		unsigned long time;

		time = wait_for_completion_timeout(&drv->device_attach, timeout);
		if (!time) {
			dev_err(drv->dev, "timed out waiting for device re-attach\n");
			return -ETIMEDOUT;
		}
	}

	regcache_cache_only(drv->dev_regmap, false);

	return 0;
}

static bool class_dev_regmap_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SDW_SCP_SDCA_INTMASK1 ... SDW_SCP_SDCA_INTMASK4:
		return false;
	default:
		return true;
	}
}

static bool class_dev_regmap_precious(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SDW_SCP_SDCA_INT1 ... SDW_SCP_SDCA_INT4:
	case SDW_SCP_SDCA_INTMASK1 ... SDW_SCP_SDCA_INTMASK4:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config class_dev_regmap_config = {
	.name			= "sdca-device",
	.reg_bits		= 32,
	.val_bits		= 8,

	.max_register		= SDW_SDCA_MAX_REGISTER,
	.volatile_reg		= class_dev_regmap_volatile,
	.precious_reg		= class_dev_regmap_precious,

	.cache_type		= REGCACHE_MAPLE,

	.lock			= class_regmap_lock,
	.unlock			= class_regmap_unlock,
};

static void class_boot_work(struct work_struct *work)
{
	struct sdca_class_drv *drv = container_of(work,
						  struct sdca_class_drv,
						  boot_work);
	int ret;

	ret = class_wait_for_attach(drv);
	if (ret)
		goto err;

	drv->irq_info = sdca_irq_allocate(drv->dev, drv->dev_regmap,
					  drv->sdw->irq);
	if (IS_ERR(drv->irq_info))
		goto err;

	ret = sdca_dev_register_functions(drv->sdw);
	if (ret)
		goto err;

	dev_dbg(drv->dev, "boot work complete\n");

	pm_runtime_mark_last_busy(drv->dev);
	pm_runtime_put_autosuspend(drv->dev);

	return;

err:
	pm_runtime_put_sync(drv->dev);
}

static void class_dev_remove(void *data)
{
	struct sdca_class_drv *drv = data;

	cancel_work_sync(&drv->boot_work);

	sdca_dev_unregister_functions(drv->sdw);
}

static int class_sdw_probe(struct sdw_slave *sdw, const struct sdw_device_id *id)
{
	struct device *dev = &sdw->dev;
	struct sdca_device_data *data = &sdw->sdca_data;
	struct regmap_config *dev_config;
	struct sdca_class_drv *drv;
	int ret;

	sdca_lookup_swft(sdw);

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	dev_config = devm_kmemdup(dev, &class_dev_regmap_config,
				  sizeof(*dev_config), GFP_KERNEL);
	if (!dev_config)
		return -ENOMEM;

	drv->functions = devm_kcalloc(dev, data->num_functions,
				      sizeof(*drv->functions),
				      GFP_KERNEL);
	if (!drv->functions)
		return -ENOMEM;

	drv->dev = dev;
	drv->sdw = sdw;
	mutex_init(&drv->regmap_lock);
	mutex_init(&drv->init_lock);

	dev_set_drvdata(drv->dev, drv);

	INIT_WORK(&drv->boot_work, class_boot_work);
	init_completion(&drv->device_attach);

	dev_config->lock_arg = &drv->regmap_lock;

	drv->dev_regmap = devm_regmap_init_sdw(sdw, dev_config);
	if (IS_ERR(drv->dev_regmap))
		return dev_err_probe(drv->dev, PTR_ERR(drv->dev_regmap),
				     "failed to create device regmap\n");

	regcache_cache_only(drv->dev_regmap, true);

	pm_runtime_set_autosuspend_delay(dev, 250);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, class_dev_remove, drv);
	if (ret)
		return ret;

	queue_work(system_long_wq, &drv->boot_work);

	return 0;
}

static int class_suspend(struct device *dev)
{
	struct sdca_class_drv *drv = dev_get_drvdata(dev);
	int ret;

	disable_irq(drv->sdw->irq);

	ret = pm_runtime_force_suspend(dev);
	if (ret) {
		dev_err(dev, "failed to force suspend: %d\n", ret);
		return ret;
	}

	return 0;
}

static int class_resume(struct device *dev)
{
	struct sdca_class_drv *drv = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret) {
		dev_err(dev, "failed to force resume: %d\n", ret);
		return ret;
	}

	enable_irq(drv->sdw->irq);

	return 0;
}

static int class_runtime_suspend(struct device *dev)
{
	struct sdca_class_drv *drv = dev_get_drvdata(dev);

	/*
	 * Whilst the driver doesn't power the chip down here, going into runtime
	 * suspend lets the SoundWire bus power down, which means the driver
	 * can't communicate with the device any more.
	 */
	regcache_cache_only(drv->dev_regmap, true);

	return 0;
}

static int class_runtime_resume(struct device *dev)
{
	struct sdca_class_drv *drv = dev_get_drvdata(dev);
	int ret;

	ret = class_wait_for_attach(drv);
	if (ret)
		goto err;

	regcache_mark_dirty(drv->dev_regmap);

	ret = regcache_sync(drv->dev_regmap);
	if (ret) {
		dev_err(drv->dev, "failed to restore cache: %d\n", ret);
		goto err;
	}

	return 0;

err:
	regcache_cache_only(drv->dev_regmap, true);

	return ret;
}

static const struct dev_pm_ops class_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(class_suspend, class_resume)
	RUNTIME_PM_OPS(class_runtime_suspend, class_runtime_resume, NULL)
};

static const struct sdw_device_id class_sdw_id[] = {
	SDW_SLAVE_ENTRY(0x01FA, 0x4245, 0),
	{}
};
MODULE_DEVICE_TABLE(sdw, class_sdw_id);

static struct sdw_driver class_sdw_driver = {
	.driver = {
		.name		= "sdca_class",
		.pm		= pm_ptr(&class_pm_ops),
	},

	.probe		= class_sdw_probe,
	.id_table	= class_sdw_id,
	.ops		= &class_sdw_ops,
};
module_sdw_driver(class_sdw_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SDCA Class Driver");
MODULE_IMPORT_NS("SND_SOC_SDCA");
