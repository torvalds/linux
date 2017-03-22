/*
 * motu.h - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_FIREWIRE_MOTU_H_INCLUDED
#define SOUND_FIREWIRE_MOTU_H_INCLUDED

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <sound/control.h>
#include <sound/core.h>

struct snd_motu {
	struct snd_card *card;
	struct fw_unit *unit;
	struct mutex mutex;
};

#endif
