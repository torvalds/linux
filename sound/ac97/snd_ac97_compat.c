/*
 *  Copyright (C) 2016 Robert Jarzmik <robert.jarzmik@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <sound/ac97/codec.h>
#include <sound/ac97/compat.h>
#include <sound/ac97/controller.h>
#include <sound/soc.h>

#include "ac97_core.h"

static void compat_ac97_release(struct device *dev)
{
	kfree(to_ac97_t(dev));
}

static void compat_ac97_reset(struct snd_ac97 *ac97)
{
	struct ac97_codec_device *adev = to_ac97_device(ac97->private_data);
	struct ac97_controller *actrl = adev->ac97_ctrl;

	if (actrl->ops->reset)
		actrl->ops->reset(actrl);
}

static void compat_ac97_warm_reset(struct snd_ac97 *ac97)
{
	struct ac97_codec_device *adev = to_ac97_device(ac97->private_data);
	struct ac97_controller *actrl = adev->ac97_ctrl;

	if (actrl->ops->warm_reset)
		actrl->ops->warm_reset(actrl);
}

static void compat_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
			      unsigned short val)
{
	struct ac97_codec_device *adev = to_ac97_device(ac97->private_data);
	struct ac97_controller *actrl = adev->ac97_ctrl;

	actrl->ops->write(actrl, ac97->num, reg, val);
}

static unsigned short compat_ac97_read(struct snd_ac97 *ac97,
				       unsigned short reg)
{
	struct ac97_codec_device *adev = to_ac97_device(ac97->private_data);
	struct ac97_controller *actrl = adev->ac97_ctrl;

	return actrl->ops->read(actrl, ac97->num, reg);
}

static struct snd_ac97_bus_ops compat_snd_ac97_bus_ops = {
	.reset = compat_ac97_reset,
	.warm_reset = compat_ac97_warm_reset,
	.write = compat_ac97_write,
	.read = compat_ac97_read,
};

static struct snd_ac97_bus compat_soc_ac97_bus = {
	.ops = &compat_snd_ac97_bus_ops,
};

struct snd_ac97 *snd_ac97_compat_alloc(struct ac97_codec_device *adev)
{
	struct snd_ac97 *ac97;
	int ret;

	ac97 = kzalloc(sizeof(struct snd_ac97), GFP_KERNEL);
	if (ac97 == NULL)
		return ERR_PTR(-ENOMEM);

	ac97->private_data = adev;
	ac97->bus = &compat_soc_ac97_bus;

	ac97->dev.parent = &adev->dev;
	ac97->dev.release = compat_ac97_release;
	dev_set_name(&ac97->dev, "%s-compat", dev_name(&adev->dev));
	ret = device_register(&ac97->dev);
	if (ret) {
		put_device(&ac97->dev);
		return ERR_PTR(ret);
	}

	return ac97;
}
EXPORT_SYMBOL_GPL(snd_ac97_compat_alloc);

void snd_ac97_compat_release(struct snd_ac97 *ac97)
{
	device_unregister(&ac97->dev);
}
EXPORT_SYMBOL_GPL(snd_ac97_compat_release);

int snd_ac97_reset(struct snd_ac97 *ac97, bool try_warm, unsigned int id,
	unsigned int id_mask)
{
	struct ac97_codec_device *adev = to_ac97_device(ac97->private_data);
	struct ac97_controller *actrl = adev->ac97_ctrl;
	unsigned int scanned;

	if (try_warm) {
		compat_ac97_warm_reset(ac97);
		scanned = snd_ac97_bus_scan_one(actrl, adev->num);
		if (ac97_ids_match(scanned, adev->vendor_id, id_mask))
			return 1;
	}

	compat_ac97_reset(ac97);
	compat_ac97_warm_reset(ac97);
	scanned = snd_ac97_bus_scan_one(actrl, adev->num);
	if (ac97_ids_match(scanned, adev->vendor_id, id_mask))
		return 0;

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(snd_ac97_reset);
