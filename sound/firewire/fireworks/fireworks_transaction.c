/*
 * fireworks_transaction.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

/*
 * Fireworks have its own transaction. The transaction can be delivered by AV/C
 * Vendor Specific command. But at least Windows driver and firmware version 5.5
 * or later don't use it.
 *
 * Transaction substance:
 *  At first, 6 data exist. Following to the 6 data, parameters for each
 *  commands exists. All of parameters are 32 bit alighed to big endian.
 *   data[0]:	Length of transaction substance
 *   data[1]:	Transaction version
 *   data[2]:	Sequence number. This is incremented by the device
 *   data[3]:	transaction category
 *   data[4]:	transaction command
 *   data[5]:	return value in response.
 *   data[6-]:	parameters
 *
 * Transaction address:
 *  command:	0xecc000000000
 *  response:	0xecc080000000 (default)
 *
 * I note that the address for response can be changed by command. But this
 * module uses the default address.
 */
#include "./fireworks.h"

#define MEMORY_SPACE_EFW_COMMAND	0xecc000000000
#define MEMORY_SPACE_EFW_RESPONSE	0xecc080000000

#define ERROR_RETRIES 3
#define ERROR_DELAY_MS 5
#define EFC_TIMEOUT_MS 125

static DEFINE_SPINLOCK(transaction_queues_lock);
static LIST_HEAD(transaction_queues);

enum transaction_queue_state {
	STATE_PENDING,
	STATE_BUS_RESET,
	STATE_COMPLETE
};

struct transaction_queue {
	struct list_head list;
	struct fw_unit *unit;
	void *buf;
	unsigned int size;
	u32 seqnum;
	enum transaction_queue_state state;
	wait_queue_head_t wait;
};

int snd_efw_transaction_run(struct fw_unit *unit,
			    const void *cmd, unsigned int cmd_size,
			    void *resp, unsigned int resp_size)
{
	struct transaction_queue t;
	unsigned int tries;
	int ret;

	t.unit = unit;
	t.buf = resp;
	t.size = resp_size;
	t.seqnum = be32_to_cpu(((struct snd_efw_transaction *)cmd)->seqnum) + 1;
	t.state = STATE_PENDING;
	init_waitqueue_head(&t.wait);

	spin_lock_irq(&transaction_queues_lock);
	list_add_tail(&t.list, &transaction_queues);
	spin_unlock_irq(&transaction_queues_lock);

	tries = 0;
	do {
		ret = snd_fw_transaction(unit, TCODE_WRITE_BLOCK_REQUEST,
					 MEMORY_SPACE_EFW_COMMAND,
					 (void *)cmd, cmd_size, 0);
		if (ret < 0)
			break;

		wait_event_timeout(t.wait, t.state != STATE_PENDING,
				   msecs_to_jiffies(EFC_TIMEOUT_MS));

		if (t.state == STATE_COMPLETE) {
			ret = t.size;
			break;
		} else if (t.state == STATE_BUS_RESET) {
			msleep(ERROR_DELAY_MS);
		} else if (++tries >= ERROR_RETRIES) {
			dev_err(&t.unit->device, "EFW transaction timed out\n");
			ret = -EIO;
			break;
		}
	} while (1);

	spin_lock_irq(&transaction_queues_lock);
	list_del(&t.list);
	spin_unlock_irq(&transaction_queues_lock);

	return ret;
}

static void
efw_response(struct fw_card *card, struct fw_request *request,
	     int tcode, int destination, int source,
	     int generation, unsigned long long offset,
	     void *data, size_t length, void *callback_data)
{
	struct fw_device *device;
	struct transaction_queue *t;
	unsigned long flags;
	int rcode;
	u32 seqnum;

	rcode = RCODE_TYPE_ERROR;
	if (length < sizeof(struct snd_efw_transaction)) {
		rcode = RCODE_DATA_ERROR;
		goto end;
	} else if (offset != MEMORY_SPACE_EFW_RESPONSE) {
		rcode = RCODE_ADDRESS_ERROR;
		goto end;
	}

	seqnum = be32_to_cpu(((struct snd_efw_transaction *)data)->seqnum);

	spin_lock_irqsave(&transaction_queues_lock, flags);
	list_for_each_entry(t, &transaction_queues, list) {
		device = fw_parent_device(t->unit);
		if ((device->card != card) ||
		    (device->generation != generation))
			continue;
		smp_rmb();	/* node_id vs. generation */
		if (device->node_id != source)
			continue;

		if ((t->state == STATE_PENDING) && (t->seqnum == seqnum)) {
			t->state = STATE_COMPLETE;
			t->size = min_t(unsigned int, length, t->size);
			memcpy(t->buf, data, t->size);
			wake_up(&t->wait);
			rcode = RCODE_COMPLETE;
		}
	}
	spin_unlock_irqrestore(&transaction_queues_lock, flags);
end:
	fw_send_response(card, request, rcode);
}

void snd_efw_transaction_bus_reset(struct fw_unit *unit)
{
	struct transaction_queue *t;

	spin_lock_irq(&transaction_queues_lock);
	list_for_each_entry(t, &transaction_queues, list) {
		if ((t->unit == unit) &&
		    (t->state == STATE_PENDING)) {
			t->state = STATE_BUS_RESET;
			wake_up(&t->wait);
		}
	}
	spin_unlock_irq(&transaction_queues_lock);
}

static struct fw_address_handler resp_register_handler = {
	.length = SND_EFW_RESPONSE_MAXIMUM_BYTES,
	.address_callback = efw_response
};

int snd_efw_transaction_register(void)
{
	static const struct fw_address_region resp_register_region = {
		.start	= MEMORY_SPACE_EFW_RESPONSE,
		.end	= MEMORY_SPACE_EFW_RESPONSE +
			  SND_EFW_RESPONSE_MAXIMUM_BYTES
	};
	return fw_core_add_address_handler(&resp_register_handler,
					   &resp_register_region);
}

void snd_efw_transaction_unregister(void)
{
	WARN_ON(!list_empty(&transaction_queues));
	fw_core_remove_address_handler(&resp_register_handler);
}
