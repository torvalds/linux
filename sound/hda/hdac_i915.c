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

/**
 * snd_hdac_set_codec_wakeup - Enable / disable HDMI/DP codec wakeup
 * @bus: HDA core bus
 * @enable: enable or disable the wakeup
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with i915 graphics.
 *
 * This function should be called during the chip reset, also called at
 * resume for updating STATESTS register read.
 *
 * Returns zero for success or a negative error code.
 */
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

/**
 * snd_hdac_display_power - Power up / down the power refcount
 * @bus: HDA core bus
 * @enable: power up or down
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with i915 graphics.
 *
 * This function manages a refcount and calls the i915 get_power() and
 * put_power() ops accordingly, toggling the codec wakeup, too.
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_display_power(struct hdac_bus *bus, bool enable)
{
	struct i915_audio_component *acomp = bus->audio_component;

	if (!acomp || !acomp->ops)
		return -ENODEV;

	dev_dbg(bus->dev, "display power %s\n",
		enable ? "enable" : "disable");

	if (enable) {
		if (!bus->i915_power_refcount++) {
			acomp->ops->get_power(acomp->dev);
			snd_hdac_set_codec_wakeup(bus, true);
			snd_hdac_set_codec_wakeup(bus, false);
		}
	} else {
		WARN_ON(!bus->i915_power_refcount);
		if (!--bus->i915_power_refcount)
			acomp->ops->put_power(acomp->dev);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_display_power);

/**
 * snd_hdac_get_display_clk - Get CDCLK in kHz
 * @bus: HDA core bus
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with i915 graphics.
 *
 * This function queries CDCLK value in kHz from the graphics driver and
 * returns the value.  A negative code is returned in error.
 */
int snd_hdac_get_display_clk(struct hdac_bus *bus)
{
	struct i915_audio_component *acomp = bus->audio_component;

	if (!acomp || !acomp->ops)
		return -ENODEV;

	return acomp->ops->get_cdclk_freq(acomp->dev);
}
EXPORT_SYMBOL_GPL(snd_hdac_get_display_clk);

/* There is a fixed mapping between audio pin node and display port
 * on current Intel platforms:
 * Pin Widget 5 - PORT B (port = 1 in i915 driver)
 * Pin Widget 6 - PORT C (port = 2 in i915 driver)
 * Pin Widget 7 - PORT D (port = 3 in i915 driver)
 */
static int pin2port(hda_nid_t pin_nid)
{
	if (WARN_ON(pin_nid < 5 || pin_nid > 7))
		return -1;
	return pin_nid - 4;
}

/**
 * snd_hdac_sync_audio_rate - Set N/CTS based on the sample rate
 * @bus: HDA core bus
 * @nid: the pin widget NID
 * @rate: the sample rate to set
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with i915 graphics.
 *
 * This function sets N/CTS value based on the given sample rate.
 * Returns zero for success, or a negative error code.
 */
int snd_hdac_sync_audio_rate(struct hdac_bus *bus, hda_nid_t nid, int rate)
{
	struct i915_audio_component *acomp = bus->audio_component;
	int port;

	if (!acomp || !acomp->ops || !acomp->ops->sync_audio_rate)
		return -ENODEV;
	port = pin2port(nid);
	if (port < 0)
		return -EINVAL;
	return acomp->ops->sync_audio_rate(acomp->dev, port, rate);
}
EXPORT_SYMBOL_GPL(snd_hdac_sync_audio_rate);

/**
 * snd_hdac_acomp_get_eld - Get the audio state and ELD via component
 * @bus: HDA core bus
 * @nid: the pin widget NID
 * @audio_enabled: the pointer to store the current audio state
 * @buffer: the buffer pointer to store ELD bytes
 * @max_bytes: the max bytes to be stored on @buffer
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with i915 graphics.
 *
 * This function queries the current state of the audio on the given
 * digital port and fetches the ELD bytes onto the given buffer.
 * It returns the number of bytes for the total ELD data, zero for
 * invalid ELD, or a negative error code.
 *
 * The return size is the total bytes required for the whole ELD bytes,
 * thus it may be over @max_bytes.  If it's over @max_bytes, it implies
 * that only a part of ELD bytes have been fetched.
 */
int snd_hdac_acomp_get_eld(struct hdac_bus *bus, hda_nid_t nid,
			   bool *audio_enabled, char *buffer, int max_bytes)
{
	struct i915_audio_component *acomp = bus->audio_component;
	int port;

	if (!acomp || !acomp->ops || !acomp->ops->get_eld)
		return -ENODEV;

	port = pin2port(nid);
	if (port < 0)
		return -EINVAL;
	return acomp->ops->get_eld(acomp->dev, port, audio_enabled,
				   buffer, max_bytes);
}
EXPORT_SYMBOL_GPL(snd_hdac_acomp_get_eld);

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

/**
 * snd_hdac_i915_register_notifier - Register i915 audio component ops
 * @aops: i915 audio component ops
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with i915 graphics.
 *
 * This function sets the given ops to be called by the i915 graphics driver.
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_i915_register_notifier(const struct i915_audio_component_audio_ops *aops)
{
	if (WARN_ON(!hdac_acomp))
		return -ENODEV;

	hdac_acomp->audio_ops = aops;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_i915_register_notifier);

/**
 * snd_hdac_i915_init - Initialize i915 audio component
 * @bus: HDA core bus
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with i915 graphics.
 *
 * This function initializes and sets up the audio component to communicate
 * with i915 graphics driver.
 *
 * Returns zero for success or a negative error code.
 */
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
	dev_info(dev, "failed to add i915 component master (%d)\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_hdac_i915_init);

/**
 * snd_hdac_i915_exit - Finalize i915 audio component
 * @bus: HDA core bus
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with i915 graphics.
 *
 * This function releases the i915 audio component that has been used.
 *
 * Returns zero for success or a negative error code.
 */
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
