/*
 *  hda_i915.c - routines for Haswell HDA controller power well support
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/component.h>
#include <drm/i915_component.h>
#include <sound/core.h>
#include "hda_controller.h"
#include "hda_intel.h"

/* Intel HSW/BDW display HDA controller Extended Mode registers.
 * EM4 (M value) and EM5 (N Value) are used to convert CDClk (Core Display
 * Clock) to 24MHz BCLK: BCLK = CDCLK * M / N
 * The values will be lost when the display power well is disabled.
 */
#define AZX_REG_EM4			0x100c
#define AZX_REG_EM5			0x1010

int hda_set_codec_wakeup(struct hda_intel *hda, bool enable)
{
	struct i915_audio_component *acomp = &hda->audio_component;

	if (!acomp->ops)
		return -ENODEV;

	if (!acomp->ops->codec_wake_override) {
		dev_warn(&hda->chip.pci->dev,
			"Invalid codec wake callback\n");
		return 0;
	}

	dev_dbg(&hda->chip.pci->dev, "%s codec wakeup\n",
		enable ? "enable" : "disable");

	acomp->ops->codec_wake_override(acomp->dev, enable);

	return 0;
}

int hda_display_power(struct hda_intel *hda, bool enable)
{
	struct i915_audio_component *acomp = &hda->audio_component;

	if (!acomp->ops)
		return -ENODEV;

	dev_dbg(&hda->chip.pci->dev, "display power %s\n",
		enable ? "enable" : "disable");

	if (enable) {
		if (!hda->i915_power_refcount++)
			acomp->ops->get_power(acomp->dev);
	} else {
		WARN_ON(!hda->i915_power_refcount);
		if (!--hda->i915_power_refcount)
			acomp->ops->put_power(acomp->dev);
	}

	return 0;
}

void haswell_set_bclk(struct hda_intel *hda)
{
	int cdclk_freq;
	unsigned int bclk_m, bclk_n;
	struct i915_audio_component *acomp = &hda->audio_component;
	struct pci_dev *pci = hda->chip.pci;

	/* Only Haswell/Broadwell need set BCLK */
	if (pci->device != 0x0a0c && pci->device != 0x0c0c
	   && pci->device != 0x0d0c && pci->device != 0x160c)
		return;

	if (!acomp->ops)
		return;

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

	azx_writew(&hda->chip, EM4, bclk_m);
	azx_writew(&hda->chip, EM5, bclk_n);
}

static int hda_component_master_bind(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	struct i915_audio_component *acomp = &hda->audio_component;
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

static void hda_component_master_unbind(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	struct i915_audio_component *acomp = &hda->audio_component;

	module_put(acomp->ops->owner);
	component_unbind_all(dev, acomp);
	WARN_ON(acomp->ops || acomp->dev);
}

static const struct component_master_ops hda_component_master_ops = {
	.bind = hda_component_master_bind,
	.unbind = hda_component_master_unbind,
};

static int hda_component_master_match(struct device *dev, void *data)
{
	/* i915 is the only supported component */
	return !strcmp(dev->driver->name, "i915");
}

int hda_i915_init(struct hda_intel *hda)
{
	struct component_match *match = NULL;
	struct device *dev = &hda->chip.pci->dev;
	struct i915_audio_component *acomp = &hda->audio_component;
	int ret;

	component_match_add(dev, &match, hda_component_master_match, hda);
	ret = component_master_add_with_match(dev, &hda_component_master_ops,
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
	component_master_del(dev, &hda_component_master_ops);
out_err:
	dev_err(dev, "failed to add i915 component master (%d)\n", ret);

	return ret;
}

int hda_i915_exit(struct hda_intel *hda)
{
	struct device *dev = &hda->chip.pci->dev;
	struct i915_audio_component *acomp = &hda->audio_component;

	WARN_ON(hda->i915_power_refcount);
	if (hda->i915_power_refcount > 0 && acomp->ops)
		acomp->ops->put_power(acomp->dev);

	component_master_del(dev, &hda_component_master_ops);

	return 0;
}
