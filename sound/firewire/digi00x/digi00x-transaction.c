// SPDX-License-Identifier: GPL-2.0-only
/*
 * digi00x-transaction.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 */

#include <sound/asound.h>
#include "digi00x.h"

static void handle_unknown_message(struct snd_dg00x *dg00x,
				   unsigned long long offset, __be32 *buf)
{
	scoped_guard(spinlock_irqsave, &dg00x->lock) {
		dg00x->msg = be32_to_cpu(*buf);
	}

	wake_up(&dg00x->hwdep_wait);
}

static void handle_message(struct fw_card *card, struct fw_request *request,
			   int tcode, int destination, int source,
			   int generation, unsigned long long offset,
			   void *data, size_t length, void *callback_data)
{
	struct snd_dg00x *dg00x = callback_data;
	__be32 *buf = (__be32 *)data;

	fw_send_response(card, request, RCODE_COMPLETE);

	if (offset == dg00x->async_handler.offset)
		handle_unknown_message(dg00x, offset, buf);
}

int snd_dg00x_transaction_reregister(struct snd_dg00x *dg00x)
{
	struct fw_device *device = fw_parent_device(dg00x->unit);
	__be32 data[2];

	/* Unknown. 4bytes. */
	data[0] = cpu_to_be32((device->card->node_id << 16) |
			      (dg00x->async_handler.offset >> 32));
	data[1] = cpu_to_be32(dg00x->async_handler.offset);
	return snd_fw_transaction(dg00x->unit, TCODE_WRITE_BLOCK_REQUEST,
				  DG00X_ADDR_BASE + DG00X_OFFSET_MESSAGE_ADDR,
				  &data, sizeof(data), 0);
}

void snd_dg00x_transaction_unregister(struct snd_dg00x *dg00x)
{
	if (dg00x->async_handler.callback_data == NULL)
		return;

	fw_core_remove_address_handler(&dg00x->async_handler);

	dg00x->async_handler.callback_data = NULL;
}

int snd_dg00x_transaction_register(struct snd_dg00x *dg00x)
{
	static const struct fw_address_region resp_register_region = {
		.start	= 0xffffe0000000ull,
		.end	= 0xffffe000ffffull,
	};
	int err;

	dg00x->async_handler.length = 4;
	dg00x->async_handler.address_callback = handle_message;
	dg00x->async_handler.callback_data = dg00x;

	err = fw_core_add_address_handler(&dg00x->async_handler,
					  &resp_register_region);
	if (err < 0)
		return err;

	err = snd_dg00x_transaction_reregister(dg00x);
	if (err < 0)
		snd_dg00x_transaction_unregister(dg00x);

	return err;
}
