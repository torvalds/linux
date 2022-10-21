// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  hdac_i915.c - routines for sync between HD-A core and i915 display driver
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/hdaudio.h>
#include <sound/hda_i915.h>
#include <sound/hda_register.h>

#define IS_HSW_CONTROLLER(pci) (((pci)->device == 0x0a0c) || \
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
	if (!IS_HSW_CONTROLLER(pci))
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

/* returns true if the devices can be connected for audio */
static bool connectivity_check(struct pci_dev *i915, struct pci_dev *hdac)
{
	struct pci_bus *bus_a = i915->bus, *bus_b = hdac->bus;

	/* directly connected on the same bus */
	if (bus_a == bus_b)
		return true;

	/*
	 * on i915 discrete GPUs with embedded HDA audio, the two
	 * devices are connected via 2nd level PCI bridge
	 */
	bus_a = bus_a->parent;
	bus_b = bus_b->parent;
	if (!bus_a || !bus_b)
		return false;
	bus_a = bus_a->parent;
	bus_b = bus_b->parent;
	if (bus_a && bus_a == bus_b)
		return true;

	return false;
}

static int i915_component_master_match(struct device *dev, int subcomponent,
				       void *data)
{
	struct pci_dev *hdac_pci, *i915_pci;
	struct hdac_bus *bus = data;

	if (!dev_is_pci(dev))
		return 0;

	hdac_pci = to_pci_dev(bus->dev);
	i915_pci = to_pci_dev(dev);

	if (!strcmp(dev->driver->name, "i915") &&
	    subcomponent == I915_COMPONENT_AUDIO &&
	    connectivity_check(i915_pci, hdac_pci))
		return 1;

	return 0;
}

/* check whether Intel graphics is present and reachable */
static int i915_gfx_present(struct pci_dev *hdac_pci)
{
	struct pci_dev *display_dev = NULL;

	for_each_pci_dev(display_dev) {
		if (display_dev->vendor == PCI_VENDOR_ID_INTEL &&
		    (display_dev->class >> 16) == PCI_BASE_CLASS_DISPLAY &&
		    connectivity_check(display_dev, hdac_pci)) {
			pci_dev_put(display_dev);
			return true;
		}
	}

	return false;
}

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

	if (!i915_gfx_present(to_pci_dev(bus->dev)))
		return -ENODEV;

	err = snd_hdac_acomp_init(bus, NULL,
				  i915_component_master_match,
				  sizeof(struct i915_audio_component) - sizeof(*acomp));
	if (err < 0)
		return err;
	acomp = bus->audio_component;
	if (!acomp)
		return -ENODEV;
	if (!acomp->ops) {
		if (!IS_ENABLED(CONFIG_MODULES) ||
		    !request_module("i915")) {
			/* 60s timeout */
			wait_for_completion_killable_timeout(&acomp->master_bind_complete,
							     msecs_to_jiffies(60 * 1000));
		}
	}
	if (!acomp->ops) {
		dev_info(bus->dev, "couldn't bind with audio component\n");
		snd_hdac_acomp_exit(bus);
		return -ENODEV;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_i915_init);
