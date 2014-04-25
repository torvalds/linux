/*
 * bebob.h - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_BEBOB_H_INCLUDED
#define SOUND_BEBOB_H_INCLUDED

#include <linux/compat.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/initval.h>

#include "../lib.h"
#include "../fcp.h"

/* basic register addresses on DM1000/DM1100/DM1500 */
#define BEBOB_ADDR_REG_INFO	0xffffc8020000
#define BEBOB_ADDR_REG_REQ	0xffffc8021000

struct snd_bebob {
	struct snd_card *card;
	struct fw_unit *unit;
	int card_index;

	struct mutex mutex;
	spinlock_t lock;
};

static inline int
snd_bebob_read_block(struct fw_unit *unit, u64 addr, void *buf, int size)
{
	return snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
				  BEBOB_ADDR_REG_INFO + addr,
				  buf, size, 0);
}

static inline int
snd_bebob_read_quad(struct fw_unit *unit, u64 addr, u32 *buf)
{
	return snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
				  BEBOB_ADDR_REG_INFO + addr,
				  (void *)buf, sizeof(u32), 0);
}

#define SND_BEBOB_DEV_ENTRY(vendor, model) \
{ \
	.match_flags	= IEEE1394_MATCH_VENDOR_ID | \
			  IEEE1394_MATCH_MODEL_ID, \
	.vendor_id	= vendor, \
	.model_id	= model, \
}

#endif
