/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2016 Robert Jarzmik <robert.jarzmik@free.fr>
 */

#ifndef AC97_CONTROLLER_H
#define AC97_CONTROLLER_H

#include <linux/device.h>
#include <linux/list.h>

#define AC97_BUS_MAX_CODECS 4
#define AC97_SLOTS_AVAILABLE_ALL 0xf

struct ac97_controller_ops;

/**
 * struct ac97_controller - The AC97 controller of the AC-Link
 * @ops:		the AC97 operations.
 * @controllers:	linked list of all existing controllers.
 * @adap:		the shell device ac97-%d, ie. ac97 adapter
 * @nr:			the number of the shell device
 * @slots_available:	the mask of accessible/scanable codecs.
 * @parent:		the device providing the AC97 controller.
 * @codecs:		the 4 possible AC97 codecs (NULL if none found).
 * @codecs_pdata:	platform_data for each codec (NULL if no pdata).
 *
 * This structure is internal to AC97 bus, and should not be used by the
 * controllers themselves, excepting for using @dev.
 */
struct ac97_controller {
	const struct ac97_controller_ops *ops;
	struct list_head controllers;
	struct device adap;
	int nr;
	unsigned short slots_available;
	struct device *parent;
	struct ac97_codec_device *codecs[AC97_BUS_MAX_CODECS];
	void *codecs_pdata[AC97_BUS_MAX_CODECS];
};

/**
 * struct ac97_controller_ops - The AC97 operations
 * @reset:	Cold reset of the AC97 AC-Link.
 * @warm_reset:	Warm reset of the AC97 AC-Link.
 * @read:	Read of a single AC97 register.
 *		Returns the register value or a negative error code.
 * @write:	Write of a single AC97 register.
 *
 * These are the basic operation an AC97 controller must provide for an AC97
 * access functions. Amongst these, all but the last 2 are mandatory.
 * The slot number is also known as the AC97 codec number, between 0 and 3.
 */
struct ac97_controller_ops {
	void (*reset)(struct ac97_controller *adrv);
	void (*warm_reset)(struct ac97_controller *adrv);
	int (*write)(struct ac97_controller *adrv, int slot,
		     unsigned short reg, unsigned short val);
	int (*read)(struct ac97_controller *adrv, int slot, unsigned short reg);
};

#if IS_ENABLED(CONFIG_AC97_BUS_NEW)
struct ac97_controller *snd_ac97_controller_register(
	const struct ac97_controller_ops *ops, struct device *dev,
	unsigned short slots_available, void **codecs_pdata);
void snd_ac97_controller_unregister(struct ac97_controller *ac97_ctrl);
#else
static inline struct ac97_controller *
snd_ac97_controller_register(const struct ac97_controller_ops *ops,
			     struct device *dev,
			     unsigned short slots_available,
			     void **codecs_pdata)
{
	return ERR_PTR(-ENODEV);
}

static inline void
snd_ac97_controller_unregister(struct ac97_controller *ac97_ctrl)
{
}
#endif

#endif
