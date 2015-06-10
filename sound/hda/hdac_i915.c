/*
 *  hdac_i915.c - routines for sync between HD-A core and i915 display driver
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/component.h>
#include <drm/i915_component.h>
#include <sound/core.h>
#include <sound/hdaudio.h>
#include <sound/hda_i915.h>

static struct i915_audio_component *hdac_acomp;

int snd_hdac_set_codec_wakeup(struct hdac_bus *bus, bool enable)
{
	struct i915_audio_component *acomp = bus->audio_component;

	if (!acomp || !acomp->ops)
		return -ENODEV;

	if (!acomp->ops->codec_wake_override) {
		dev_warn(bus->dev,
			"Invalid codec wake callback\n");
		return 0;
	}

	dev_dbg(bus->dev, "%s codec wakeup\n",
		enable ? "enable" : "disable");

	acomp->ops->codec_wake_override(acomp->dev, enable);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_set_codec_wakeup);

int snd_hdac_display_power(struct hdac_bus *bus, bool enable)
{
	struct i915_audio_component *acomp = bus->audio_component;

	if (!acomp || !acomp->ops)
		return -ENODEV;

	dev_dbg(bus->dev, "display power %s\n",
		enable ? "enable" : "disable");

	if (enable) {
		if (!bus->i915_power_refcount++)
			acomp->ops->get_power(acomp->dev);
	} else {
		WARN_ON(!bus->i915_power_refcount);
		if (!--bus->i915_power_refcount)
			acomp->ops->put_power(acomp->dev);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_display_power);

int snd_hdac_get_display_clk(struct hdac_bus *bus)
{
	struct i915_audio_component *acomp = bus->audio_component;

	if (!acomp || !acomp->ops)
		return -ENODEV;

	return acomp->ops->get_cdclk_freq(acomp->dev);
}
EXPORT_SYMBOL_GPL(snd_hdac_get_display_clk);

static int hdac_component_master_bind(struct device *dev)
{
	struct i915_audio_component *acomp = hdac_acomp;
	int ret;

	ret = component_bind_all(dev, acomp);
	if (ret < 0)
		return ret;

	if (WARN_ON(!(acomp->dev && acomp->ops && acomp->ops->get_power &&
		      acomp->ops->put_power && acomp->ops->get_cdclk_freq))) {
		ret = -EINVAL;
		goto out_unbind;
	}

	/*
	 * Atm, we don't support dynamic unbinding initiated by the child
	 * component, so pin its containing module until we unbind.
	 */
	if (!try_module_get(acomp->ops->owner)) {
		ret = -ENODEV;
		goto out_unbind;
	}

	return 0;

out_unbind:
	component_unbind_all(dev, acomp);

	return ret;
}

static void hdac_component_master_unbind(struct device *dev)
{
	struct i915_audio_component *acomp = hdac_acomp;

	module_put(acomp->ops->owner);
	component_unbind_all(dev, acomp);
	WARN_ON(acomp->ops || acomp->dev);
}

static const struct component_master_ops hdac_component_master_ops = {
	.bind = hdac_component_master_bind,
	.unbind = hdac_component_master_unbind,
};

static int hdac_component_master_match(struct device *dev, void *data)
{
	/* i915 is the only supported component */
	return !strcmp(dev->driver->name, "i915");
}

int snd_hdac_i915_init(struct hdac_bus *bus)
{
	struct component_match *match = NULL;
	struct device *dev = bus->dev;
	struct i915_audio_component *acomp;
	int ret;

	acomp = kzalloc(sizeof(*acomp), GFP_KERNEL);
	if (!acomp)
		return -ENOMEM;
	bus->audio_component = acomp;
	hdac_acomp = acomp;

	component_match_add(dev, &match, hdac_component_master_match, bus);
	ret = component_master_add_with_match(dev, &hdac_component_master_ops,
					      match);
	if (ret < 0)
		goto out_err;

	/*
	 * Atm, we don't support deferring the component binding, so make sure
	 * i915 is loaded and that the binding successfully completes.
	 */
	request_module("i915");

	if (!acomp->ops) {
		ret = -ENODEV;
		goto out_master_del;
	}
	dev_dbg(dev, "bound to i915 component master\n");

	return 0;
out_master_del:
	component_master_del(dev, &hdac_component_master_ops);
out_err:
	kfree(acomp);
	bus->audio_component = NULL;
	dev_err(dev, "failed to add i915 component master (%d)\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_hdac_i915_init);

int snd_hdac_i915_exit(struct hdac_bus *bus)
{
	struct device *dev = bus->dev;
	struct i915_audio_component *acomp = bus->audio_component;

	if (!acomp)
		return 0;

	WARN_ON(bus->i915_power_refcount);
	if (bus->i915_power_refcount > 0 && acomp->ops)
		acomp->ops->put_power(acomp->dev);

	component_master_del(dev, &hdac_component_master_ops);

	kfree(acomp);
	bus->audio_component = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_i915_exit);
