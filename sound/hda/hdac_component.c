// SPDX-License-Identifier: GPL-2.0
// hdac_component.c - routines for sync between HD-A core and DRM driver

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/component.h>
#include <linux/string_choices.h>
#include <sound/core.h>
#include <sound/hdaudio.h>
#include <sound/hda_component.h>
#include <sound/hda_register.h>

static void hdac_acomp_release(struct device *dev, void *res)
{
}

static struct drm_audio_component *hdac_get_acomp(struct device *dev)
{
	return devres_find(dev, hdac_acomp_release, NULL, NULL);
}

/**
 * snd_hdac_set_codec_wakeup - Enable / disable HDMI/DP codec wakeup
 * @bus: HDA core bus
 * @enable: enable or disable the wakeup
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with graphics driver.
 *
 * This function should be called during the chip reset, also called at
 * resume for updating STATESTS register read.
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_set_codec_wakeup(struct hdac_bus *bus, bool enable)
{
	struct drm_audio_component *acomp = bus->audio_component;

	if (!acomp || !acomp->ops)
		return -ENODEV;

	if (!acomp->ops->codec_wake_override)
		return 0;

	dev_dbg(bus->dev, "%s codec wakeup\n", str_enable_disable(enable));

	acomp->ops->codec_wake_override(acomp->dev, enable);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_set_codec_wakeup);

/**
 * snd_hdac_display_power - Power up / down the power refcount
 * @bus: HDA core bus
 * @idx: HDA codec address, pass HDA_CODEC_IDX_CONTROLLER for controller
 * @enable: power up or down
 *
 * This function is used by either HD-audio controller or codec driver that
 * needs the interaction with graphics driver.
 *
 * This function updates the power status, and calls the get_power() and
 * put_power() ops accordingly, toggling the codec wakeup, too.
 */
void snd_hdac_display_power(struct hdac_bus *bus, unsigned int idx, bool enable)
{
	struct drm_audio_component *acomp = bus->audio_component;

	dev_dbg(bus->dev, "display power %s\n", str_enable_disable(enable));

	mutex_lock(&bus->lock);
	if (enable)
		set_bit(idx, &bus->display_power_status);
	else
		clear_bit(idx, &bus->display_power_status);

	if (!acomp || !acomp->ops)
		goto unlock;

	if (bus->display_power_status) {
		if (!bus->display_power_active) {
			unsigned long cookie = -1;

			if (acomp->ops->get_power)
				cookie = acomp->ops->get_power(acomp->dev);

			snd_hdac_set_codec_wakeup(bus, true);
			snd_hdac_set_codec_wakeup(bus, false);
			bus->display_power_active = cookie;
		}
	} else {
		if (bus->display_power_active) {
			unsigned long cookie = bus->display_power_active;

			if (acomp->ops->put_power)
				acomp->ops->put_power(acomp->dev, cookie);

			bus->display_power_active = 0;
		}
	}
 unlock:
	mutex_unlock(&bus->lock);
}
EXPORT_SYMBOL_GPL(snd_hdac_display_power);

/**
 * snd_hdac_sync_audio_rate - Set N/CTS based on the sample rate
 * @codec: HDA codec
 * @nid: the pin widget NID
 * @dev_id: device identifier
 * @rate: the sample rate to set
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with graphics driver.
 *
 * This function sets N/CTS value based on the given sample rate.
 * Returns zero for success, or a negative error code.
 */
int snd_hdac_sync_audio_rate(struct hdac_device *codec, hda_nid_t nid,
			     int dev_id, int rate)
{
	struct hdac_bus *bus = codec->bus;
	struct drm_audio_component *acomp = bus->audio_component;
	int port, pipe;

	if (!acomp || !acomp->ops || !acomp->ops->sync_audio_rate)
		return -ENODEV;
	port = nid;
	if (acomp->audio_ops && acomp->audio_ops->pin2port) {
		port = acomp->audio_ops->pin2port(codec, nid);
		if (port < 0)
			return -EINVAL;
	}
	pipe = dev_id;
	return acomp->ops->sync_audio_rate(acomp->dev, port, pipe, rate);
}
EXPORT_SYMBOL_GPL(snd_hdac_sync_audio_rate);

/**
 * snd_hdac_acomp_get_eld - Get the audio state and ELD via component
 * @codec: HDA codec
 * @nid: the pin widget NID
 * @dev_id: device identifier
 * @audio_enabled: the pointer to store the current audio state
 * @buffer: the buffer pointer to store ELD bytes
 * @max_bytes: the max bytes to be stored on @buffer
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with graphics driver.
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
int snd_hdac_acomp_get_eld(struct hdac_device *codec, hda_nid_t nid, int dev_id,
			   bool *audio_enabled, char *buffer, int max_bytes)
{
	struct hdac_bus *bus = codec->bus;
	struct drm_audio_component *acomp = bus->audio_component;
	int port, pipe;

	if (!acomp || !acomp->ops || !acomp->ops->get_eld)
		return -ENODEV;

	port = nid;
	if (acomp->audio_ops && acomp->audio_ops->pin2port) {
		port = acomp->audio_ops->pin2port(codec, nid);
		if (port < 0)
			return -EINVAL;
	}
	pipe = dev_id;
	return acomp->ops->get_eld(acomp->dev, port, pipe, audio_enabled,
				   buffer, max_bytes);
}
EXPORT_SYMBOL_GPL(snd_hdac_acomp_get_eld);

static int hdac_component_master_bind(struct device *dev)
{
	struct drm_audio_component *acomp = hdac_get_acomp(dev);
	int ret;

	if (WARN_ON(!acomp))
		return -EINVAL;

	ret = component_bind_all(dev, acomp);
	if (ret < 0)
		return ret;

	if (WARN_ON(!(acomp->dev && acomp->ops))) {
		ret = -EINVAL;
		goto out_unbind;
	}

	/* pin the module to avoid dynamic unbinding, but only if given */
	if (!try_module_get(acomp->ops->owner)) {
		ret = -ENODEV;
		goto out_unbind;
	}

	if (acomp->audio_ops && acomp->audio_ops->master_bind) {
		ret = acomp->audio_ops->master_bind(dev, acomp);
		if (ret < 0)
			goto module_put;
	}

	complete_all(&acomp->master_bind_complete);
	return 0;

 module_put:
	module_put(acomp->ops->owner);
out_unbind:
	component_unbind_all(dev, acomp);
	complete_all(&acomp->master_bind_complete);

	return ret;
}

static void hdac_component_master_unbind(struct device *dev)
{
	struct drm_audio_component *acomp = hdac_get_acomp(dev);

	if (acomp->audio_ops && acomp->audio_ops->master_unbind)
		acomp->audio_ops->master_unbind(dev, acomp);
	module_put(acomp->ops->owner);
	component_unbind_all(dev, acomp);
	WARN_ON(acomp->ops || acomp->dev);
}

static const struct component_master_ops hdac_component_master_ops = {
	.bind = hdac_component_master_bind,
	.unbind = hdac_component_master_unbind,
};

/**
 * snd_hdac_acomp_register_notifier - Register audio component ops
 * @bus: HDA core bus
 * @aops: audio component ops
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with graphics driver.
 *
 * This function sets the given ops to be called by the graphics driver.
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_acomp_register_notifier(struct hdac_bus *bus,
				    const struct drm_audio_component_audio_ops *aops)
{
	if (!bus->audio_component)
		return -ENODEV;

	bus->audio_component->audio_ops = aops;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_acomp_register_notifier);

/**
 * snd_hdac_acomp_init - Initialize audio component
 * @bus: HDA core bus
 * @aops: audio component ops
 * @match_master: match function for finding components
 * @extra_size: Extra bytes to allocate
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with graphics driver.
 *
 * This function initializes and sets up the audio component to communicate
 * with graphics driver.
 *
 * Unlike snd_hdac_i915_init(), this function doesn't synchronize with the
 * binding with the DRM component.  Each caller needs to sync via master_bind
 * audio_ops.
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_acomp_init(struct hdac_bus *bus,
			const struct drm_audio_component_audio_ops *aops,
			int (*match_master)(struct device *, int, void *),
			size_t extra_size)
{
	struct component_match *match = NULL;
	struct device *dev = bus->dev;
	struct drm_audio_component *acomp;
	int ret;

	if (WARN_ON(hdac_get_acomp(dev)))
		return -EBUSY;

	acomp = devres_alloc(hdac_acomp_release, sizeof(*acomp) + extra_size,
			     GFP_KERNEL);
	if (!acomp)
		return -ENOMEM;
	acomp->audio_ops = aops;
	init_completion(&acomp->master_bind_complete);
	bus->audio_component = acomp;
	devres_add(dev, acomp);

	component_match_add_typed(dev, &match, match_master, bus);
	ret = component_master_add_with_match(dev, &hdac_component_master_ops,
					      match);
	if (ret < 0)
		goto out_err;

	return 0;

out_err:
	bus->audio_component = NULL;
	devres_destroy(dev, hdac_acomp_release, NULL, NULL);
	dev_info(dev, "failed to add audio component master (%d)\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_hdac_acomp_init);

/**
 * snd_hdac_acomp_exit - Finalize audio component
 * @bus: HDA core bus
 *
 * This function is supposed to be used only by a HD-audio controller
 * driver that needs the interaction with graphics driver.
 *
 * This function releases the audio component that has been used.
 *
 * Returns zero for success or a negative error code.
 */
int snd_hdac_acomp_exit(struct hdac_bus *bus)
{
	struct device *dev = bus->dev;
	struct drm_audio_component *acomp = bus->audio_component;

	if (!acomp)
		return 0;

	if (WARN_ON(bus->display_power_active) && acomp->ops)
		acomp->ops->put_power(acomp->dev, bus->display_power_active);

	bus->display_power_active = 0;
	bus->display_power_status = 0;

	component_master_del(dev, &hdac_component_master_ops);

	bus->audio_component = NULL;
	devres_destroy(dev, hdac_acomp_release, NULL, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_acomp_exit);
