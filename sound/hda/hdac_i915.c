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
#include <sound/core.h>
#include <sound/hdaudio.h>
#include <sound/hda_i915.h>
#include <sound/hda_register.h>

static struct completion bind_complete;

#define CONTROLLER_IN_GPU(pci) (((pci)->device == 0x0a0c) || \
				((pci)->device == 0x0c0c) || \
				((pci)->device == 0x0d0c) || \
				((pci)->device == 0x160c))

/**
 * snd_hdac_i915_set_bclk - Reprogram BCLK for HSW/BDW
 * @bus: HDA core bus
 *
 * Intel HSW/BDW display HDA controller is in GPU. Both its power and link BCLK
 * depends on GPU. Two Extended Mode registers EM4 (M value) and EM5 (N Value)
 * are used to convert CDClk (Core Display Clock) to 24MHz BCLK:
 * BCLK = CDCLK * M / N
 * The values will be lost when the display power well is disabled and need to
 * be restored to avoid abnormal playback speed.
 *
 * Call this function at initializing and changing power well, as well as
 * at ELD notifier for the hotplug.
 */
void snd_hdac_i915_set_bclk(struct hdac_bus *bus)
{
	struct drm_audio_component *acomp = bus->audio_component;
	struct pci_dev *pci = to_pci_dev(bus->dev);
	int cdclk_freq;
	unsigned int bclk_m, bclk_n;

	if (!acomp || !acomp->ops || !acomp->ops->get_cdclk_freq)
		return; /* only for i915 binding */
	if (!CONTROLLER_IN_GPU(pci))
		return; /* only HSW/BDW */

	cdclk_freq = acomp->ops->get_cdclk_freq(acomp->dev);
	switch (cdclk_freq) {
	case 337500:
		bclk_m = 16;
		bclk_n = 225;
		break;

	case 450000:
	default: /* default CDCLK 450MHz */
		bclk_m = 4;
		bclk_n = 75;
		break;

	case 540000:
		bclk_m = 4;
		bclk_n = 90;
		break;

	case 675000:
		bclk_m = 8;
		bclk_n = 225;
		break;
	}

	snd_hdac_chip_writew(bus, HSW_EM4, bclk_m);
	snd_hdac_chip_writew(bus, HSW_EM5, bclk_n);
}
EXPORT_SYMBOL_GPL(snd_hdac_i915_set_bclk);

static int i915_component_master_match(struct device *dev, int subcomponent,
				       void *data)
{
	return !strcmp(dev->driver->name, "i915") &&
	       subcomponent == I915_COMPONENT_AUDIO;
}

/* check whether intel graphics is present */
static bool i915_gfx_present(void)
{
	static const struct pci_device_id ids[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_ANY_ID),
		  .class = PCI_BASE_CLASS_DISPLAY << 16,
		  .class_mask = 0xff << 16 },
		{}
	};
	return pci_dev_present(ids);
}

static int i915_master_bind(struct device *dev,
			    struct drm_audio_component *acomp)
{
	complete_all(&bind_complete);
	/* clear audio_ops here as it was needed only for completion call */
	acomp->audio_ops = NULL;
	return 0;
}

static const struct drm_audio_component_audio_ops i915_init_ops = {
	.master_bind = i915_master_bind
};

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
	struct drm_audio_component *acomp;
	int err;

	if (!i915_gfx_present())
		return -ENODEV;

	init_completion(&bind_complete);

	err = snd_hdac_acomp_init(bus, &i915_init_ops,
				  i915_component_master_match,
				  sizeof(struct i915_audio_component) - sizeof(*acomp));
	if (err < 0)
		return err;
	acomp = bus->audio_component;
	if (!acomp)
		return -ENODEV;
	if (!acomp->ops) {
		request_module("i915");
		/* 60s timeout */
		wait_for_completion_timeout(&bind_complete,
					    msecs_to_jiffies(60 * 1000));
	}
	if (!acomp->ops) {
		dev_info(bus->dev, "couldn't bind with audio component\n");
		snd_hdac_acomp_exit(bus);
		return -ENODEV;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_i915_init);
