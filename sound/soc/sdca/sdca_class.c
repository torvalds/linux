// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/device.h>
#include <linux/err.h>
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
#include <sound/sdca_class.h>

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

static const struct sdw_slave_ops class_sdw_ops = {
	.read_prop	= class_read_prop,
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

static void class_remove_functions(void *data)
{
	struct sdca_class_drv *drv = data;

	sdca_dev_unregister_functions(drv->sdw);
}

static void class_boot_work(struct work_struct *work)
{
	struct sdca_class_drv *drv = container_of(work,
						  struct sdca_class_drv,
						  boot_work);
	int ret;

	ret = sdw_slave_wait_for_init(drv->sdw, CLASS_SDW_ATTACH_TIMEOUT_MS);
	if (ret)
		goto err;

	regcache_cache_only(drv->dev_regmap, false);

	drv->irq_info = devm_sdca_irq_allocate(drv->dev, drv->dev_regmap,
					       drv->sdw->irq);
	if (IS_ERR(drv->irq_info))
		goto err;

	ret = sdca_dev_register_functions(drv->sdw);
	if (ret)
		goto err;

	/* Ensure function drivers are removed before the IRQ is destroyed */
	ret = devm_add_action_or_reset(drv->dev, class_remove_functions, drv);
	if (ret)
		goto err;

	dev_dbg(drv->dev, "boot work complete\n");

	pm_runtime_mark_last_busy(drv->dev);
	pm_runtime_put_autosuspend(drv->dev);

	return;

err:
	pm_runtime_put_sync(drv->dev);
}

/**
 * sdca_class_probe - SDCA class SoundWire slave probe helper
 * @sdw: SoundWire slave
 * @drv: caller-allocated sdca_class_drv storage.  The caller (a codec
 *       driver, or the built-in class_sdw_driver in this file) owns the
 *       allocation and sets its own dev_set_drvdata() -- the framework
 *       does not touch drvdata.  Typically embedded in the codec's own
 *       priv struct so codec drivers can keep per-slave state.
 * @ops: optional codec-provided class callbacks (may be NULL for
 *       pure-generic SDCA parts that need no quirks)
 *
 * Codec-specific SoundWire drivers call this from their .probe after
 * allocating a struct sdca_class_drv (usually embedded in their own
 * priv) and setting drvdata to their priv.  The framework fills in the
 * sdca_class_drv fields, sets up the class regmap, and queues the
 * deferred boot work.
 */
int sdca_class_probe(struct sdw_slave *sdw,
		     struct sdca_class_drv *drv,
		     const struct sdca_class_ops *ops)
{
	struct device *dev = &sdw->dev;
	struct regmap_config *dev_config;
	int ret;

	sdca_lookup_swft(sdw);

	if (!drv)
		return -EINVAL;

	dev_config = devm_kmemdup(dev, &class_dev_regmap_config,
				  sizeof(*dev_config), GFP_KERNEL);
	if (!dev_config)
		return -ENOMEM;

	drv->dev = dev;
	drv->sdw = sdw;
	drv->ops = ops;
	mutex_init(&drv->regmap_lock);
	mutex_init(&drv->init_lock);

	INIT_WORK(&drv->boot_work, class_boot_work);

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

	queue_work(system_long_wq, &drv->boot_work);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_class_probe, "SND_SOC_SDCA_CLASS");

static int class_sdw_probe(struct sdw_slave *sdw, const struct sdw_device_id *id)
{
	struct sdca_class_drv *drv;

	/*
	 * Pure-generic SDCA parts: no codec priv to embed, so allocate a
	 * bare sdca_class_drv here and stash it in drvdata for the
	 * built-in PM ops to fetch.
	 */
	drv = devm_kzalloc(&sdw->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	dev_set_drvdata(&sdw->dev, drv);

	return sdca_class_probe(sdw, drv, NULL);
}

/**
 * sdca_class_remove - SDCA class SoundWire slave remove helper
 * @drv: caller-owned sdca_class_drv (the one handed to sdca_class_probe()).
 *
 * Cancels the deferred boot work so devres can safely free @drv and the
 * embedding codec priv without racing class_boot_work.  Codec-specific
 * SoundWire drivers that call sdca_class_probe() must call this from
 * their .remove with the same drv pointer they passed to probe.
 */
void sdca_class_remove(struct sdca_class_drv *drv)
{
	cancel_work_sync(&drv->boot_work);
}
EXPORT_SYMBOL_NS_GPL(sdca_class_remove, "SND_SOC_SDCA_CLASS");

static void class_sdw_remove(struct sdw_slave *sdw)
{
	struct sdca_class_drv *drv = dev_get_drvdata(&sdw->dev);

	sdca_class_remove(drv);
}

/**
 * sdca_class_system_suspend - SDCA class system suspend helper
 * @drv: caller-owned sdca_class_drv.
 *
 * Codec drivers compose this into their own dev_pm_ops.  Disables the
 * SoundWire interrupt and forces runtime suspend of the underlying
 * class regmap.
 */
int sdca_class_system_suspend(struct sdca_class_drv *drv)
{
	int ret;

	disable_irq(drv->sdw->irq);

	ret = pm_runtime_force_suspend(drv->dev);
	if (ret) {
		dev_err(drv->dev, "failed to force suspend: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_class_system_suspend, "SND_SOC_SDCA_CLASS");

/**
 * sdca_class_system_resume - SDCA class system resume helper
 * @drv: caller-owned sdca_class_drv.
 */
int sdca_class_system_resume(struct sdca_class_drv *drv)
{
	int ret;

	ret = pm_runtime_force_resume(drv->dev);
	if (ret) {
		dev_err(drv->dev, "failed to force resume: %d\n", ret);
		return ret;
	}

	enable_irq(drv->sdw->irq);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_class_system_resume, "SND_SOC_SDCA_CLASS");

/**
 * sdca_class_runtime_suspend - SDCA class runtime suspend helper
 * @drv: caller-owned sdca_class_drv.
 */
int sdca_class_runtime_suspend(struct sdca_class_drv *drv)
{
	/*
	 * Whilst the driver doesn't power the chip down here, going into runtime
	 * suspend lets the SoundWire bus power down, which means the driver
	 * can't communicate with the device any more.
	 */
	regcache_cache_only(drv->dev_regmap, true);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_class_runtime_suspend, "SND_SOC_SDCA_CLASS");

/**
 * sdca_class_runtime_resume - SDCA class runtime resume helper
 * @drv: caller-owned sdca_class_drv.
 */
int sdca_class_runtime_resume(struct sdca_class_drv *drv)
{
	int ret;

	ret = sdw_slave_wait_for_init(drv->sdw, CLASS_SDW_ATTACH_TIMEOUT_MS);
	if (ret)
		goto err;

	regcache_cache_only(drv->dev_regmap, false);
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
EXPORT_SYMBOL_NS_GPL(sdca_class_runtime_resume, "SND_SOC_SDCA_CLASS");

/*
 * Convenience dev_pm_ops used by the built-in class_sdw_driver, which
 * stashes its sdca_class_drv in drvdata directly.  Codec drivers that
 * embed sdca_class_drv in their own priv compose their own dev_pm_ops
 * using the sdca_class_*_suspend/resume helpers above.
 */
static int class_pm_system_suspend(struct device *dev)
{
	return sdca_class_system_suspend(dev_get_drvdata(dev));
}

static int class_pm_system_resume(struct device *dev)
{
	return sdca_class_system_resume(dev_get_drvdata(dev));
}

static int class_pm_runtime_suspend(struct device *dev)
{
	return sdca_class_runtime_suspend(dev_get_drvdata(dev));
}

static int class_pm_runtime_resume(struct device *dev)
{
	return sdca_class_runtime_resume(dev_get_drvdata(dev));
}

static const struct dev_pm_ops sdca_class_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(class_pm_system_suspend, class_pm_system_resume)
	RUNTIME_PM_OPS(class_pm_runtime_suspend, class_pm_runtime_resume, NULL)
};

static const struct sdw_device_id class_sdw_id[] = {
	SDW_SLAVE_ENTRY(0x01FA, 0x4245, 0),
	SDW_SLAVE_ENTRY(0x01FA, 0x4249, 0),
	SDW_SLAVE_ENTRY(0x01FA, 0x4747, 0),
	{}
};
MODULE_DEVICE_TABLE(sdw, class_sdw_id);

static struct sdw_driver class_sdw_driver = {
	.driver = {
		.name		= "sdca_class",
		.pm		= pm_ptr(&sdca_class_pm_ops),
	},

	.probe		= class_sdw_probe,
	.remove		= class_sdw_remove,
	.id_table	= class_sdw_id,
	.ops		= &class_sdw_ops,
};
module_sdw_driver(class_sdw_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SDCA Class Driver");
MODULE_IMPORT_NS("SND_SOC_SDCA");
