/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux driver for M2Tech hiFace compatible devices
 *
 * Copyright 2012-2013 (C) M2TECH S.r.l and Amarula Solutions B.V.
 *
 * Authors:  Michael Trimarchi <michael@amarulasolutions.com>
 *           Antonio Ospite <ao2@amarulasolutions.com>
 *
 * The driver is based on the work done in TerraTec DMX 6Fire USB
 */

#ifndef HIFACE_CHIP_H
#define HIFACE_CHIP_H

#include <linux/usb.h>
#include <sound/core.h>

struct pcm_runtime;

struct hiface_chip {
	struct usb_device *dev;
	struct snd_card *card;
	struct pcm_runtime *pcm;
};
#endif /* HIFACE_CHIP_H */
