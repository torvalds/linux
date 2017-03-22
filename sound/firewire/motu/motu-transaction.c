/*
 * motu-transaction.c - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */


#include "motu.h"

#define SND_MOTU_ADDR_BASE	0xfffff0000000ULL
#define ASYNC_ADDR_HI  0x0b04
#define ASYNC_ADDR_LO  0x0b08

int snd_motu_transaction_read(struct snd_motu *motu, u32 offset, __be32 *reg,
			      size_t size)
{
	int tcode;

	if (size % sizeof(__be32) > 0 || size <= 0)
		return -EINVAL;
	if (size == sizeof(__be32))
		tcode = TCODE_READ_QUADLET_REQUEST;
	else
		tcode = TCODE_READ_BLOCK_REQUEST;

	return snd_fw_transaction(motu->unit, tcode,
				  SND_MOTU_ADDR_BASE + offset, reg, size, 0);
}

int snd_motu_transaction_write(struct snd_motu *motu, u32 offset, __be32 *reg,
			       size_t size)
{
	int tcode;

	if (size % sizeof(__be32) > 0 || size <= 0)
		return -EINVAL;
	if (size == sizeof(__be32))
		tcode = TCODE_WRITE_QUADLET_REQUEST;
	else
		tcode = TCODE_WRITE_BLOCK_REQUEST;

	return snd_fw_transaction(motu->unit, tcode,
				  SND_MOTU_ADDR_BASE + offset, reg, size, 0);
}

static void handle_message(struct fw_card *card, struct fw_request *request,
			   int tcode, int destination, int source,
			   int generation, unsigned long long offset,
			   void *data, size_t length, void *callback_data)
{
	struct snd_motu *motu = callback_data;
	__be32 *buf = (__be32 *)data;
	unsigned long flags;

	if (tcode != TCODE_WRITE_QUADLET_REQUEST) {
		fw_send_response(card, request, RCODE_COMPLETE);
		return;
	}

	if (offset != motu->async_handler.offset || length != 4) {
		fw_send_response(card, request, RCODE_ADDRESS_ERROR);
		return;
	}

	spin_lock_irqsave(&motu->lock, flags);
	motu->msg = be32_to_cpu(*buf);
	spin_unlock_irqrestore(&motu->lock, flags);

	fw_send_response(card, request, RCODE_COMPLETE);

	wake_up(&motu->hwdep_wait);
}

int snd_motu_transaction_reregister(struct snd_motu *motu)
{
	struct fw_device *device = fw_parent_device(motu->unit);
	__be32 data;
	int err;

	if (motu->async_handler.callback_data == NULL)
		return -EINVAL;

	/* Register messaging address. Block transaction is not allowed. */
	data = cpu_to_be32((device->card->node_id << 16) |
			   (motu->async_handler.offset >> 32));
	err = snd_motu_transaction_write(motu, ASYNC_ADDR_HI, &data,
					 sizeof(data));
	if (err < 0)
		return err;

	data = cpu_to_be32(motu->async_handler.offset);
	return snd_motu_transaction_write(motu, ASYNC_ADDR_LO, &data,
					  sizeof(data));
}

int snd_motu_transaction_register(struct snd_motu *motu)
{
	static const struct fw_address_region resp_register_region = {
		.start	= 0xffffe0000000ull,
		.end	= 0xffffe000ffffull,
	};
	int err;

	/* Perhaps, 4 byte messages are transferred. */
	motu->async_handler.length = 4;
	motu->async_handler.address_callback = handle_message;
	motu->async_handler.callback_data = motu;

	err = fw_core_add_address_handler(&motu->async_handler,
					  &resp_register_region);
	if (err < 0)
		return err;

	err = snd_motu_transaction_reregister(motu);
	if (err < 0) {
		fw_core_remove_address_handler(&motu->async_handler);
		motu->async_handler.address_callback = NULL;
	}

	return err;
}

void snd_motu_transaction_unregister(struct snd_motu *motu)
{
	__be32 data;

	if (motu->async_handler.address_callback != NULL)
		fw_core_remove_address_handler(&motu->async_handler);
	motu->async_handler.address_callback = NULL;

	/* Unregister the address. */
	data = cpu_to_be32(0x00000000);
	snd_motu_transaction_write(motu, ASYNC_ADDR_HI, &data, sizeof(data));
	snd_motu_transaction_write(motu, ASYNC_ADDR_LO, &data, sizeof(data));
}
