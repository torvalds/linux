/*
 * oxfw-scs1x.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) 2015 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

#define HSS1394_ADDRESS			0xc007dedadadaULL
#define HSS1394_MAX_PACKET_SIZE		64
#define HSS1394_TAG_CHANGE_ADDRESS	0xf1

struct fw_scs1x {
	struct fw_address_handler hss_handler;
};

static void handle_hss(struct fw_card *card, struct fw_request *request,
		       int tcode, int destination, int source, int generation,
		       unsigned long long offset, void *data, size_t length,
		       void *callback_data)
{
	fw_send_response(card, request, RCODE_COMPLETE);
}

static int register_address(struct snd_oxfw *oxfw)
{
	struct fw_scs1x *scs = oxfw->spec;
	__be64 data;

	data = cpu_to_be64(((u64)HSS1394_TAG_CHANGE_ADDRESS << 56) |
			    scs->hss_handler.offset);
	return snd_fw_transaction(oxfw->unit, TCODE_WRITE_BLOCK_REQUEST,
				  HSS1394_ADDRESS, &data, sizeof(data), 0);
}

static void remove_scs1x(struct snd_rawmidi *rmidi)
{
	struct fw_scs1x *scs = rmidi->private_data;

	fw_core_remove_address_handler(&scs->hss_handler);
}

void snd_oxfw_scs1x_update(struct snd_oxfw *oxfw)
{
	register_address(oxfw);
}

int snd_oxfw_scs1x_add(struct snd_oxfw *oxfw)
{
	struct snd_rawmidi *rmidi;
	struct fw_scs1x *scs;
	int err;

	scs = kzalloc(sizeof(struct fw_scs1x), GFP_KERNEL);
	if (scs == NULL)
		return -ENOMEM;
	oxfw->spec = scs;

	/* Allocate own handler for imcoming asynchronous transaction. */
	scs->hss_handler.length = HSS1394_MAX_PACKET_SIZE;
	scs->hss_handler.address_callback = handle_hss;
	scs->hss_handler.callback_data = scs;
	err = fw_core_add_address_handler(&scs->hss_handler,
					  &fw_high_memory_region);
	if (err < 0)
		return err;

	err = register_address(oxfw);
	if (err < 0)
		goto err_allocated;

	/* Use unique name for backward compatibility to scs1x module. */
	err = snd_rawmidi_new(oxfw->card, "SCS.1x", 0, 0, 0, &rmidi);
	if (err < 0)
		goto err_allocated;
	rmidi->private_data = scs;
	rmidi->private_free = remove_scs1x;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", oxfw->card->shortname);

	return 0;
err_allocated:
	fw_core_remove_address_handler(&scs->hss_handler);
	return err;
}
