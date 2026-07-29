// SPDX-License-Identifier: GPL-2.0-only
//
// AW88399 HDA side codec driver
//
// Based on cs35l41_hda.c and aw88399.c
//

#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/string.h>
#include <sound/hda_codec.h>
#include "../generic.h"
#include "hda_component.h"
#include "aw88399_hda.h"

#define AW88399_HDA_I2C_BASE_ADDR	0x34

static void aw88399_hda_playback_hook(struct device *dev, int action)
{
	struct aw88399_hda *aw88399 = dev_get_drvdata(dev);
	struct aw88399 *core = aw88399->core;
	int ret = 0;

	dev_dbg(aw88399->dev, "Playback action: %d\n", action);

	switch (action) {
	case HDA_GEN_PCM_ACT_OPEN:
		pm_runtime_get_sync(dev);
		aw88399->playing = true;
		break;
	case HDA_GEN_PCM_ACT_PREPARE:
		if (core)
			aw88399_start(core, AW88399_SYNC_START);
		break;
	case HDA_GEN_PCM_ACT_CLEANUP:
		if (aw88399->aw_dev)
			ret = aw88399_stop(aw88399->aw_dev);
		if (ret)
			dev_err(aw88399->dev, "Failed to stop amplifier: %d\n", ret);
		break;
	case HDA_GEN_PCM_ACT_CLOSE:
		if (aw88399->aw_dev)
			aw88399_stop(aw88399->aw_dev);
		aw88399->playing = false;
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);
		break;
	default:
		dev_warn(aw88399->dev, "Unsupported action: %d\n", action);
		break;
	}
}

static int aw88399_hda_bind(struct device *dev, struct device *master, void *master_data)
{
	struct aw88399_hda *aw88399 = dev_get_drvdata(dev);
	struct hda_component_parent *parent = master_data;
	struct hda_component *comp;

	comp = hda_component_from_index(parent, aw88399->index);
	if (!comp)
		return -EINVAL;

	if (comp->dev)
		return -EBUSY;

	comp->dev = dev;

	strscpy(comp->name, dev_name(dev), sizeof(comp->name));

	comp->playback_hook = aw88399_hda_playback_hook;

	dev_info(aw88399->dev,
		 "AW88399 Bound - SSID: %s, channel: %d\n",
		 aw88399->acpi_subsystem_id, aw88399->channel);

	return 0;
}

static void aw88399_hda_unbind(struct device *dev, struct device *master, void *master_data)
{
	struct aw88399_hda *aw88399 = dev_get_drvdata(dev);
	struct hda_component_parent *parent = master_data;
	struct hda_component *comp;

	comp = hda_component_from_index(parent, aw88399->index);
	if (comp && (comp->dev == dev))
		memset(comp, 0, sizeof(*comp));

	dev_dbg(aw88399->dev, "Unbound from HDA codec\n");
}

static const struct component_ops aw88399_hda_comp_ops = {
	.bind = aw88399_hda_bind,
	.unbind = aw88399_hda_unbind,
};

static int aw88399_hda_index_from_i2c(struct aw88399_hda *aw88399)
{
	return to_i2c_client(aw88399->dev)->addr - AW88399_HDA_I2C_BASE_ADDR;
}

static int aw88399_hda_init(struct aw88399_hda *aw88399)
{
	struct device *dev = aw88399->dev;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct aw88399 *core;
	int ret;

	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	mutex_init(&core->lock);
	core->reset_gpio = aw88399->reset_gpio;
	core->regmap = aw88399->regmap;
	core->bsts_unreliable = aw88399->bsts_unreliable;

	aw88399_hw_reset(core);

	ret = aw88399_init(core, i2c, aw88399->regmap);
	if (ret)
		return ret;

	/* Set channel BEFORE loading firmware so ACF parser sees correct value */
	if (core->aw_pa)
		aw88399_dev_set_channel(core, aw88399->channel);

	ret = aw88399_request_firmware_file(core);
	if (ret)
		return ret;

	aw88399->core = core;
	aw88399->aw_dev = core->aw_pa;

	return 0;
}

struct aw88399_prop_model {
	const char *ssid;
	int (*apply_prop)(struct aw88399_hda *aw88399);
};

static const struct aw88399_prop_model aw88399_prop_model_table[] = {
	{ }
};

static int aw88399_hda_acpi_probe(struct aw88399_hda *aw88399)
{
	struct acpi_device *adev;
	struct device *physdev;
	const char *sub;
	const struct aw88399_prop_model *model;

	aw88399->index = aw88399_hda_index_from_i2c(aw88399);
	aw88399->channel = aw88399->index;
	aw88399->acpi_subsystem_id = NULL;

	adev = acpi_dev_get_first_match_dev("AWDZ8399", NULL, -1);
	if (!adev) {
		dev_err(aw88399->dev, "Failed to find an ACPI device for AWDZ8399\n");
		return -ENODEV;
	}

	physdev = get_device(acpi_get_first_physical_node(adev));
	acpi_dev_put(adev);
	if (!physdev)
		return -ENODEV;

	sub = acpi_get_subsystem_id(ACPI_HANDLE(physdev));
	put_device(physdev);
	if (IS_ERR_OR_NULL(sub))
		return 0;

	aw88399->acpi_subsystem_id = devm_kstrdup(aw88399->dev, sub, GFP_KERNEL);
	kfree(sub);
	if (!aw88399->acpi_subsystem_id)
		return -ENOMEM;

	for (model = aw88399_prop_model_table; model->ssid; model++) {
		if (!strcasecmp(model->ssid, aw88399->acpi_subsystem_id)) {
			dev_info(aw88399->dev,
				 "Applying properties for SSID %s\n",
				 aw88399->acpi_subsystem_id);
			return model->apply_prop(aw88399);
		}
	}

	return 0;
}

int aw88399_hda_probe(struct device *dev, struct regmap *regmap)
{
	struct aw88399_hda *aw88399;
	int ret;

	aw88399 = devm_kzalloc(dev, sizeof(*aw88399), GFP_KERNEL);
	if (!aw88399)
		return -ENOMEM;

	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to obtain regmap\n");

	aw88399->dev = dev;
	aw88399->regmap = regmap;
	dev_set_drvdata(dev, aw88399);

	aw88399->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(aw88399->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(aw88399->reset_gpio),
				     "Failed to get reset GPIO\n");

	ret = aw88399_hda_acpi_probe(aw88399);
	if (ret)
		return dev_err_probe(dev, ret, "ACPI probe failed\n");

	ret = aw88399_hda_init(aw88399);
	if (ret)
		return dev_err_probe(dev, ret, "Chip initialization failed\n");

	/* Enable runtime PM */
	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = component_add(dev, &aw88399_hda_comp_ops);
	if (ret) {
		pm_runtime_disable(dev);
		return dev_err_probe(dev, ret, "Failed to register component\n");
	}

	dev_info(dev, "AW88399 HDA side codec registered successfully\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(aw88399_hda_probe, "SND_HDA_SCODEC_AW88399");

void aw88399_hda_remove(struct device *dev)
{
	struct aw88399_hda *aw88399 = dev_get_drvdata(dev);

	pm_runtime_disable(dev);

	if (aw88399->aw_dev)
		aw88399_stop(aw88399->aw_dev);

	component_del(dev, &aw88399_hda_comp_ops);

	dev_dbg(aw88399->dev, "AW88399 HDA side codec removed\n");
}
EXPORT_SYMBOL_NS_GPL(aw88399_hda_remove, "SND_HDA_SCODEC_AW88399");

static int aw88399_hda_runtime_suspend(struct device *dev)
{
	struct aw88399_hda *aw88399 = dev_get_drvdata(dev);

	dev_dbg(aw88399->dev, "Runtime suspend\n");

	if (aw88399->aw_dev && aw88399->playing)
		aw88399_stop(aw88399->aw_dev);

	return 0;
}

static int aw88399_hda_runtime_resume(struct device *dev)
{
	struct aw88399_hda *aw88399 = dev_get_drvdata(dev);

	dev_dbg(aw88399->dev, "Runtime resume\n");

	if (aw88399->core && aw88399->aw_dev && aw88399->playing)
		aw88399_start(aw88399->core, AW88399_SYNC_START);

	return 0;
}

static int aw88399_hda_system_suspend(struct device *dev)
{
	struct aw88399_hda *aw88399 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(aw88399->dev, "System suspend\n");

	if (aw88399->aw_dev && aw88399->playing)
		aw88399_stop(aw88399->aw_dev);

	if (aw88399->core)
		aw88399->core->fw_needs_reload = true;

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		dev_err(aw88399->dev, "Runtime force suspend failed: %d\n", ret);

	return ret;
}

static int aw88399_hda_system_resume(struct device *dev)
{
	struct aw88399_hda *aw88399 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(aw88399->dev, "System resume\n");

	if (aw88399->aw_dev)
		aw88399_hw_reset(aw88399->core);

	ret = pm_runtime_force_resume(dev);
	if (ret)
		dev_err(aw88399->dev, "Runtime force resume failed: %d\n", ret);

	return ret;
}

const struct dev_pm_ops aw88399_hda_pm_ops = {
	RUNTIME_PM_OPS(aw88399_hda_runtime_suspend, aw88399_hda_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(aw88399_hda_system_suspend, aw88399_hda_system_resume)
};
EXPORT_SYMBOL_NS_GPL(aw88399_hda_pm_ops, "SND_HDA_SCODEC_AW88399");

MODULE_DESCRIPTION("AW88399 HDA driver");
MODULE_AUTHOR("Yakov Till <yakov.till@gmail.com>");
MODULE_AUTHOR("Marco Giunta <marco_giunta@outlook.it>");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("aw88399_acf.bin");
