/*
 * digi00x-transaction.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <sound/asound.h>
#include "digi00x.h"

static int fill_midi_message(struct snd_rawmidi_substream *substream, u8 *buf)
{
	int bytes;

	buf[0] = 0x80;
	bytes = snd_rawmidi_transmit_peek(substream, buf + 1, 2);
	if (bytes >= 0)
		buf[3] = 0xc0 | bytes;

	return bytes;
}

static void handle_midi_control(struct snd_dg00x *dg00x, __be32 *buf,
				unsigned int length)
{
	struct snd_rawmidi_substream *substream;
	unsigned int i;
	unsigned int len;
	u8 *b;

	substream = ACCESS_ONCE(dg00x->in_control);
	if (substream == NULL)
		return;

	length /= 4;

	for (i = 0; i < length; i++) {
		b = (u8 *)&buf[i];
		len = b[3] & 0xf;
		if (len > 0)
			snd_rawmidi_receive(dg00x->in_control, b + 1, len);
	}
}

static void handle_unknown_message(struct snd_dg00x *dg00x,
				   unsigned long long offset, __be32 *buf)
{
	unsigned long flags;

	spin_lock_irqsave(&dg00x->lock, flags);
	dg00x->msg = be32_to_cpu(*buf);
	spin_unlock_irqrestore(&dg00x->lock, flags);

	wake_up(&dg00x->hwdep_wait);
}

static void handle_message(struct fw_card *card, struct fw_request *request,
			   int tcode, int destination, int source,
			   int generation, unsigned long long offset,
			   void *data, size_t length, void *callback_data)
{
	struct snd_dg00x *dg00x = callback_data;
	__be32 *buf = (__be32 *)data;

	if (offset == dg00x->async_handler.offset)
		handle_unknown_message(dg00x, offset, buf);
	else if (offset == dg00x->async_handler.offset + 4)
		handle_midi_control(dg00x, buf, length);

	fw_send_response(card, request, RCODE_COMPLETE);
}

int snd_dg00x_transaction_reregister(struct snd_dg00x *dg00x)
{
	struct fw_device *device = fw_parent_device(dg00x->unit);
	__be32 data[2];
	int err;

	/* Unknown. 4bytes. */
	data[0] = cpu_to_be32((device->card->node_id << 16) |
			      (dg00x->async_handler.offset >> 32));
	data[1] = cpu_to_be32(dg00x->async_handler.offset);
	err = snd_fw_transaction(dg00x->unit, TCODE_WRITE_BLOCK_REQUEST,
				 DG00X_ADDR_BASE + DG00X_OFFSET_MESSAGE_ADDR,
				 &data, sizeof(data), 0);
	if (err < 0)
		return err;

	/* Asynchronous transactions for MIDI control message. */
	data[0] = cpu_to_be32((device->card->node_id << 16) |
			      (dg00x->async_handler.offset >> 32));
	data[1] = cpu_to_be32(dg00x->async_handler.offset + 4);
	return snd_fw_transaction(dg00x->unit, TCODE_WRITE_BLOCK_REQUEST,
				  DG00X_ADDR_BASE + DG00X_OFFSET_MIDI_CTL_ADDR,
				  &data, sizeof(data), 0);
}

int snd_dg00x_transaction_register(struct snd_dg00x *dg00x)
{
	static const struct fw_address_region resp_register_region = {
		.start	= 0xffffe0000000ull,
		.end	= 0xffffe000ffffull,
	};
	int err;

	dg00x->async_handler.length = 12;
	dg00x->async_handler.address_callback = handle_message;
	dg00x->async_handler.callback_data = dg00x;

	err = fw_core_add_address_handler(&dg00x->async_handler,
					  &resp_register_region);
	if (err < 0)
		return err;

	err = snd_dg00x_transaction_reregister(dg00x);
	if (err < 0)
		goto error;

	err = snd_fw_async_midi_port_init(&dg00x->out_control, dg00x->unit,
					  DG00X_ADDR_BASE + DG00X_OFFSET_MMC,
					  4, fill_midi_message);
	if (err < 0)
		goto error;

	return err;
error:
	fw_core_remove_address_handler(&dg00x->async_handler);
	dg00x->async_handler.callback_data = NULL;
	return err;
}

void snd_dg00x_transaction_unregister(struct snd_dg00x *dg00x)
{
	if (dg00x->async_handler.callback_data == NULL)
		return;

	snd_fw_async_midi_port_destroy(&dg00x->out_control);
	fw_core_remove_address_handler(&dg00x->async_handler);

	dg00x->async_handler.callback_data = NULL;
}
