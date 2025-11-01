// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Robert Jarzmik <robert.jarzmik@free.fr>
 */

#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <sound/ac97_codec.h>
#include <sound/ac97/codec.h>
#include <sound/ac97/controller.h>
#include <sound/ac97/regs.h>

#include "ac97_core.h"

/*
 * Protects ac97_controllers and each ac97_controller structure.
 */
static DEFINE_MUTEX(ac97_controllers_mutex);
static DEFINE_IDR(ac97_adapter_idr);
static LIST_HEAD(ac97_controllers);

static inline struct ac97_controller*
to_ac97_controller(struct device *ac97_adapter)
{
	return container_of(ac97_adapter, struct ac97_controller, adap);
}

static int ac97_unbound_ctrl_write(struct ac97_controller *adrv, int slot,
		     unsigned short reg, unsigned short val)
{
	return -ENODEV;
}

static int ac97_unbound_ctrl_read(struct ac97_controller *adrv, int slot,
				  unsigned short reg)
{
	return -ENODEV;
}

static const struct ac97_controller_ops ac97_unbound_ctrl_ops = {
	.write = ac97_unbound_ctrl_write,
	.read = ac97_unbound_ctrl_read,
};

static struct ac97_controller ac97_unbound_ctrl = {
	.ops = &ac97_unbound_ctrl_ops,
};

static struct ac97_codec_device *
ac97_codec_find(struct ac97_controller *ac97_ctrl, unsigned int codec_num)
{
	if (codec_num >= AC97_BUS_MAX_CODECS)
		return ERR_PTR(-EINVAL);

	return ac97_ctrl->codecs[codec_num];
}

static struct device_node *
ac97_of_get_child_device(struct ac97_controller *ac97_ctrl, int idx,
			 unsigned int vendor_id)
{
	struct device_node *node;
	u32 reg;
	char compat[] = "ac97,0000,0000";

	snprintf(compat, sizeof(compat), "ac97,%04x,%04x",
		 vendor_id >> 16, vendor_id & 0xffff);

	for_each_child_of_node(ac97_ctrl->parent->of_node, node) {
		if ((idx != of_property_read_u32(node, "reg", &reg)) ||
		    !of_device_is_compatible(node, compat))
			continue;
		return node;
	}

	return NULL;
}

static void ac97_codec_release(struct device *dev)
{
	struct ac97_codec_device *adev;
	struct ac97_controller *ac97_ctrl;

	adev = to_ac97_device(dev);
	ac97_ctrl = adev->ac97_ctrl;
	ac97_ctrl->codecs[adev->num] = NULL;
	of_node_put(dev->of_node);
	kfree(adev);
}

static int ac97_codec_add(struct ac97_controller *ac97_ctrl, int idx,
		   unsigned int vendor_id)
{
	struct ac97_codec_device *codec;
	int ret;

	codec = kzalloc(sizeof(*codec), GFP_KERNEL);
	if (!codec)
		return -ENOMEM;
	ac97_ctrl->codecs[idx] = codec;
	codec->vendor_id = vendor_id;
	codec->dev.release = ac97_codec_release;
	codec->dev.bus = &ac97_bus_type;
	codec->dev.parent = &ac97_ctrl->adap;
	codec->num = idx;
	codec->ac97_ctrl = ac97_ctrl;

	device_initialize(&codec->dev);
	dev_set_name(&codec->dev, "%s:%u", dev_name(ac97_ctrl->parent), idx);
	codec->dev.of_node = ac97_of_get_child_device(ac97_ctrl, idx,
						      vendor_id);

	ret = device_add(&codec->dev);
	if (ret) {
		put_device(&codec->dev);
		return ret;
	}

	return 0;
}

unsigned int snd_ac97_bus_scan_one(struct ac97_controller *adrv,
				   unsigned int codec_num)
{
	unsigned short vid1, vid2;
	int ret;

	ret = adrv->ops->read(adrv, codec_num, AC97_VENDOR_ID1);
	vid1 = (ret & 0xffff);
	if (ret < 0)
		return 0;

	ret = adrv->ops->read(adrv, codec_num, AC97_VENDOR_ID2);
	vid2 = (ret & 0xffff);
	if (ret < 0)
		return 0;

	dev_dbg(&adrv->adap, "%s(codec_num=%u): vendor_id=0x%08x\n",
		__func__, codec_num, AC97_ID(vid1, vid2));
	return AC97_ID(vid1, vid2);
}

static int ac97_bus_scan(struct ac97_controller *ac97_ctrl)
{
	int ret, i;
	unsigned int vendor_id;

	for (i = 0; i < AC97_BUS_MAX_CODECS; i++) {
		if (ac97_codec_find(ac97_ctrl, i))
			continue;
		if (!(ac97_ctrl->slots_available & BIT(i)))
			continue;
		vendor_id = snd_ac97_bus_scan_one(ac97_ctrl, i);
		if (!vendor_id)
			continue;

		ret = ac97_codec_add(ac97_ctrl, i, vendor_id);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int ac97_bus_reset(struct ac97_controller *ac97_ctrl)
{
	ac97_ctrl->ops->reset(ac97_ctrl);

	return 0;
}

/**
 * snd_ac97_codec_driver_register - register an AC97 codec driver
 * @drv: AC97 driver codec to register
 *
 * Register an AC97 codec driver to the ac97 bus driver, aka. the AC97 digital
 * controller.
 *
 * Returns 0 on success or error code
 */
int snd_ac97_codec_driver_register(struct ac97_codec_driver *drv)
{
	drv->driver.bus = &ac97_bus_type;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(snd_ac97_codec_driver_register);

/**
 * snd_ac97_codec_driver_unregister - unregister an AC97 codec driver
 * @drv: AC97 codec driver to unregister
 *
 * Unregister a previously registered ac97 codec driver.
 */
void snd_ac97_codec_driver_unregister(struct ac97_codec_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(snd_ac97_codec_driver_unregister);

/**
 * snd_ac97_codec_get_platdata - get platform_data
 * @adev: the ac97 codec device
 *
 * For legacy platforms, in order to have platform_data in codec drivers
 * available, while ac97 device are auto-created upon probe, this retrieves the
 * platdata which was setup on ac97 controller registration.
 *
 * Returns the platform data pointer
 */
void *snd_ac97_codec_get_platdata(const struct ac97_codec_device *adev)
{
	struct ac97_controller *ac97_ctrl = adev->ac97_ctrl;

	return ac97_ctrl->codecs_pdata[adev->num];
}
EXPORT_SYMBOL_GPL(snd_ac97_codec_get_platdata);

static void ac97_ctrl_codecs_unregister(struct ac97_controller *ac97_ctrl)
{
	int i;

	for (i = 0; i < AC97_BUS_MAX_CODECS; i++)
		if (ac97_ctrl->codecs[i]) {
			ac97_ctrl->codecs[i]->ac97_ctrl = &ac97_unbound_ctrl;
			device_unregister(&ac97_ctrl->codecs[i]->dev);
		}
}

static ssize_t cold_reset_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	struct ac97_controller *ac97_ctrl;

	guard(mutex)(&ac97_controllers_mutex);
	ac97_ctrl = to_ac97_controller(dev);
	ac97_ctrl->ops->reset(ac97_ctrl);
	return len;
}
static DEVICE_ATTR_WO(cold_reset);

static ssize_t warm_reset_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	struct ac97_controller *ac97_ctrl;

	if (!dev)
		return -ENODEV;

	guard(mutex)(&ac97_controllers_mutex);
	ac97_ctrl = to_ac97_controller(dev);
	ac97_ctrl->ops->warm_reset(ac97_ctrl);
	return len;
}
static DEVICE_ATTR_WO(warm_reset);

static struct attribute *ac97_controller_device_attrs[] = {
	&dev_attr_cold_reset.attr,
	&dev_attr_warm_reset.attr,
	NULL
};

static const struct attribute_group ac97_adapter_attr_group = {
	.name	= "ac97_operations",
	.attrs	= ac97_controller_device_attrs,
};

static const struct attribute_group *ac97_adapter_groups[] = {
	&ac97_adapter_attr_group,
	NULL,
};

static void ac97_del_adapter(struct ac97_controller *ac97_ctrl)
{
	scoped_guard(mutex, &ac97_controllers_mutex) {
		ac97_ctrl_codecs_unregister(ac97_ctrl);
		list_del(&ac97_ctrl->controllers);
	}

	device_unregister(&ac97_ctrl->adap);
}

static void ac97_adapter_release(struct device *dev)
{
	struct ac97_controller *ac97_ctrl;

	ac97_ctrl = to_ac97_controller(dev);
	idr_remove(&ac97_adapter_idr, ac97_ctrl->nr);
	dev_dbg(&ac97_ctrl->adap, "adapter unregistered by %s\n",
		dev_name(ac97_ctrl->parent));
}

static const struct device_type ac97_adapter_type = {
	.groups		= ac97_adapter_groups,
	.release	= ac97_adapter_release,
};

static int ac97_add_adapter(struct ac97_controller *ac97_ctrl)
{
	int ret;

	guard(mutex)(&ac97_controllers_mutex);
	ret = idr_alloc(&ac97_adapter_idr, ac97_ctrl, 0, 0, GFP_KERNEL);
	ac97_ctrl->nr = ret;
	if (ret >= 0) {
		dev_set_name(&ac97_ctrl->adap, "ac97-%d", ret);
		ac97_ctrl->adap.type = &ac97_adapter_type;
		ac97_ctrl->adap.parent = ac97_ctrl->parent;
		ret = device_register(&ac97_ctrl->adap);
		if (ret)
			put_device(&ac97_ctrl->adap);
	}
	if (!ret) {
		list_add(&ac97_ctrl->controllers, &ac97_controllers);
		dev_dbg(&ac97_ctrl->adap, "adapter registered by %s\n",
			dev_name(ac97_ctrl->parent));
	}
	return ret;
}

/**
 * snd_ac97_controller_register - register an ac97 controller
 * @ops: the ac97 bus operations
 * @dev: the device providing the ac97 DC function
 * @slots_available: mask of the ac97 codecs that can be scanned and probed
 *                   bit0 => codec 0, bit1 => codec 1 ... bit 3 => codec 3
 * @codecs_pdata: codec platform data
 *
 * Register a digital controller which can control up to 4 ac97 codecs. This is
 * the controller side of the AC97 AC-link, while the slave side are the codecs.
 *
 * Returns a valid controller upon success, negative pointer value upon error
 */
struct ac97_controller *snd_ac97_controller_register(
	const struct ac97_controller_ops *ops, struct device *dev,
	unsigned short slots_available, void **codecs_pdata)
{
	struct ac97_controller *ac97_ctrl;
	int ret, i;

	ac97_ctrl = kzalloc(sizeof(*ac97_ctrl), GFP_KERNEL);
	if (!ac97_ctrl)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < AC97_BUS_MAX_CODECS && codecs_pdata; i++)
		ac97_ctrl->codecs_pdata[i] = codecs_pdata[i];

	ac97_ctrl->ops = ops;
	ac97_ctrl->slots_available = slots_available;
	ac97_ctrl->parent = dev;
	ret = ac97_add_adapter(ac97_ctrl);

	if (ret)
		goto err;
	ac97_bus_reset(ac97_ctrl);
	ac97_bus_scan(ac97_ctrl);

	return ac97_ctrl;
err:
	kfree(ac97_ctrl);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(snd_ac97_controller_register);

/**
 * snd_ac97_controller_unregister - unregister an ac97 controller
 * @ac97_ctrl: the device previously provided to ac97_controller_register()
 *
 */
void snd_ac97_controller_unregister(struct ac97_controller *ac97_ctrl)
{
	ac97_del_adapter(ac97_ctrl);
}
EXPORT_SYMBOL_GPL(snd_ac97_controller_unregister);

static int ac97_pm_runtime_suspend(struct device *dev)
{
	struct ac97_codec_device *codec = to_ac97_device(dev);
	int ret = pm_generic_runtime_suspend(dev);

	if (ret == 0 && dev->driver) {
		if (pm_runtime_is_irq_safe(dev))
			clk_disable(codec->clk);
		else
			clk_disable_unprepare(codec->clk);
	}

	return ret;
}

static int ac97_pm_runtime_resume(struct device *dev)
{
	struct ac97_codec_device *codec = to_ac97_device(dev);
	int ret;

	if (dev->driver) {
		if (pm_runtime_is_irq_safe(dev))
			ret = clk_enable(codec->clk);
		else
			ret = clk_prepare_enable(codec->clk);
		if (ret)
			return ret;
	}

	return pm_generic_runtime_resume(dev);
}

static const struct dev_pm_ops ac97_pm = {
	.suspend	= pm_generic_suspend,
	.resume		= pm_generic_resume,
	.freeze		= pm_generic_freeze,
	.thaw		= pm_generic_thaw,
	.poweroff	= pm_generic_poweroff,
	.restore	= pm_generic_restore,
	RUNTIME_PM_OPS(ac97_pm_runtime_suspend, ac97_pm_runtime_resume, NULL)
};

static int ac97_get_enable_clk(struct ac97_codec_device *adev)
{
	int ret;

	adev->clk = clk_get(&adev->dev, "ac97_clk");
	if (IS_ERR(adev->clk))
		return PTR_ERR(adev->clk);

	ret = clk_prepare_enable(adev->clk);
	if (ret)
		clk_put(adev->clk);

	return ret;
}

static void ac97_put_disable_clk(struct ac97_codec_device *adev)
{
	clk_disable_unprepare(adev->clk);
	clk_put(adev->clk);
}

static ssize_t vendor_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ac97_codec_device *codec = to_ac97_device(dev);

	return sysfs_emit(buf, "%08x", codec->vendor_id);
}
static DEVICE_ATTR_RO(vendor_id);

static struct attribute *ac97_dev_attrs[] = {
	&dev_attr_vendor_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ac97_dev);

static int ac97_bus_match(struct device *dev, const struct device_driver *drv)
{
	struct ac97_codec_device *adev = to_ac97_device(dev);
	const struct ac97_codec_driver *adrv = to_ac97_driver(drv);
	const struct ac97_id *id = adrv->id_table;
	int i = 0;

	if (adev->vendor_id == 0x0 || adev->vendor_id == 0xffffffff)
		return false;

	do {
		if (ac97_ids_match(id[i].id, adev->vendor_id, id[i].mask))
			return true;
	} while (id[i++].id);

	return false;
}

static int ac97_bus_probe(struct device *dev)
{
	struct ac97_codec_device *adev = to_ac97_device(dev);
	struct ac97_codec_driver *adrv = to_ac97_driver(dev->driver);
	int ret;

	ret = ac97_get_enable_clk(adev);
	if (ret)
		return ret;

	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = adrv->probe(adev);
	if (ret == 0)
		return 0;

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);
	ac97_put_disable_clk(adev);

	return ret;
}

static void ac97_bus_remove(struct device *dev)
{
	struct ac97_codec_device *adev = to_ac97_device(dev);
	struct ac97_codec_driver *adrv = to_ac97_driver(dev->driver);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return;

	adrv->remove(adev);
	pm_runtime_put_noidle(dev);
	ac97_put_disable_clk(adev);

	pm_runtime_disable(dev);
}

const struct bus_type ac97_bus_type = {
	.name		= "ac97bus",
	.dev_groups	= ac97_dev_groups,
	.match		= ac97_bus_match,
	.pm		= pm_ptr(&ac97_pm),
	.probe		= ac97_bus_probe,
	.remove		= ac97_bus_remove,
};

static int __init ac97_bus_init(void)
{
	return bus_register(&ac97_bus_type);
}
subsys_initcall(ac97_bus_init);

static void __exit ac97_bus_exit(void)
{
	bus_unregister(&ac97_bus_type);
}
module_exit(ac97_bus_exit);

MODULE_DESCRIPTION("AC97 bus interface");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Jarzmik <robert.jarzmik@free.fr>");
