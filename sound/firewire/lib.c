/*
 * miscellaneous helper functions
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "lib.h"

#define ERROR_RETRY_DELAY_MS	20

/**
 * snd_fw_transaction - send a request and wait for its completion
 * @unit: the driver's unit on the target device
 * @tcode: the transaction code
 * @offset: the address in the target's address space
 * @buffer: input/output data
 * @length: length of @buffer
 * @flags: use %FW_FIXED_GENERATION and add the generation value to attempt the
 *         request only in that generation; use %FW_QUIET to suppress error
 *         messages
 *
 * Submits an asynchronous request to the target device, and waits for the
 * response.  The node ID and the current generation are derived from @unit.
 * On a bus reset or an error, the transaction is retried a few times.
 * Returns zero on success, or a negative error code.
 */
int snd_fw_transaction(struct fw_unit *unit, int tcode,
		       u64 offset, void *buffer, size_t length,
		       unsigned int flags)
{
	struct fw_device *device = fw_parent_device(unit);
	int generation, rcode, tries = 0;

	generation = flags & FW_GENERATION_MASK;
	for (;;) {
		if (!(flags & FW_FIXED_GENERATION)) {
			generation = device->generation;
			smp_rmb(); /* node_id vs. generation */
		}
		rcode = fw_run_transaction(device->card, tcode,
					   device->node_id, generation,
					   device->max_speed, offset,
					   buffer, length);

		if (rcode == RCODE_COMPLETE)
			return 0;

		if (rcode == RCODE_GENERATION && (flags & FW_FIXED_GENERATION))
			return -EAGAIN;

		if (rcode_is_permanent_error(rcode) || ++tries >= 3) {
			if (!(flags & FW_QUIET))
				dev_err(&unit->device,
					"transaction failed: %s\n",
					fw_rcode_string(rcode));
			return -EIO;
		}

		msleep(ERROR_RETRY_DELAY_MS);
	}
}
EXPORT_SYMBOL(snd_fw_transaction);

#define PROBE_DELAY_MS		(2 * MSEC_PER_SEC)

/**
 * snd_fw_schedule_registration - schedule work for sound card registration
 * @unit: an instance for unit on IEEE 1394 bus
 * @dwork: delayed work with callback function
 *
 * This function is not designed for general purposes. When new unit is
 * connected to IEEE 1394 bus, the bus is under bus-reset state because of
 * topological change. In this state, units tend to fail both of asynchronous
 * and isochronous communication. To avoid this problem, this function is used
 * to postpone sound card registration after the state. The callers must
 * set up instance of delayed work in advance.
 */
void snd_fw_schedule_registration(struct fw_unit *unit,
				  struct delayed_work *dwork)
{
	u64 now, delay;

	now = get_jiffies_64();
	delay = fw_parent_device(unit)->card->reset_jiffies
					+ msecs_to_jiffies(PROBE_DELAY_MS);

	if (time_after64(delay, now))
		delay -= now;
	else
		delay = 0;

	mod_delayed_work(system_wq, dwork, delay);
}
EXPORT_SYMBOL(snd_fw_schedule_registration);

static void async_midi_port_callback(struct fw_card *card, int rcode,
				     void *data, size_t length,
				     void *callback_data)
{
	struct snd_fw_async_midi_port *port = callback_data;
	struct snd_rawmidi_substream *substream = ACCESS_ONCE(port->substream);

	/* This port is closed. */
	if (substream == NULL)
		return;

	if (rcode == RCODE_COMPLETE)
		snd_rawmidi_transmit_ack(substream, port->consume_bytes);
	else if (!rcode_is_permanent_error(rcode))
		/* To start next transaction immediately for recovery. */
		port->next_ktime = 0;
	else
		/* Don't continue processing. */
		port->error = true;

	port->idling = true;

	if (!snd_rawmidi_transmit_empty(substream))
		schedule_work(&port->work);
}

static void midi_port_work(struct work_struct *work)
{
	struct snd_fw_async_midi_port *port =
			container_of(work, struct snd_fw_async_midi_port, work);
	struct snd_rawmidi_substream *substream = ACCESS_ONCE(port->substream);
	int generation;
	int type;

	/* Under transacting or error state. */
	if (!port->idling || port->error)
		return;

	/* Nothing to do. */
	if (substream == NULL || snd_rawmidi_transmit_empty(substream))
		return;

	/* Do it in next chance. */
	if (ktime_after(port->next_ktime, ktime_get())) {
		schedule_work(&port->work);
		return;
	}

	/*
	 * Fill the buffer. The callee must use snd_rawmidi_transmit_peek().
	 * Later, snd_rawmidi_transmit_ack() is called.
	 */
	memset(port->buf, 0, port->len);
	port->consume_bytes = port->fill(substream, port->buf);
	if (port->consume_bytes <= 0) {
		/* Do it in next chance, immediately. */
		if (port->consume_bytes == 0) {
			port->next_ktime = 0;
			schedule_work(&port->work);
		} else {
			/* Fatal error. */
			port->error = true;
		}
		return;
	}

	/* Calculate type of transaction. */
	if (port->len == 4)
		type = TCODE_WRITE_QUADLET_REQUEST;
	else
		type = TCODE_WRITE_BLOCK_REQUEST;

	/* Set interval to next transaction. */
	port->next_ktime = ktime_add_ns(ktime_get(),
				port->consume_bytes * 8 * NSEC_PER_SEC / 31250);

	/* Start this transaction. */
	port->idling = false;

	/*
	 * In Linux FireWire core, when generation is updated with memory
	 * barrier, node id has already been updated. In this module, After
	 * this smp_rmb(), load/store instructions to memory are completed.
	 * Thus, both of generation and node id are available with recent
	 * values. This is a light-serialization solution to handle bus reset
	 * events on IEEE 1394 bus.
	 */
	generation = port->parent->generation;
	smp_rmb();

	fw_send_request(port->parent->card, &port->transaction, type,
			port->parent->node_id, generation,
			port->parent->max_speed, port->addr,
			port->buf, port->len, async_midi_port_callback,
			port);
}

/**
 * snd_fw_async_midi_port_init - initialize asynchronous MIDI port structure
 * @port: the asynchronous MIDI port to initialize
 * @unit: the target of the asynchronous transaction
 * @addr: the address to which transactions are transferred
 * @len: the length of transaction
 * @fill: the callback function to fill given buffer, and returns the
 *	       number of consumed bytes for MIDI message.
 *
 */
int snd_fw_async_midi_port_init(struct snd_fw_async_midi_port *port,
		struct fw_unit *unit, u64 addr, unsigned int len,
		snd_fw_async_midi_port_fill fill)
{
	port->len = DIV_ROUND_UP(len, 4) * 4;
	port->buf = kzalloc(port->len, GFP_KERNEL);
	if (port->buf == NULL)
		return -ENOMEM;

	port->parent = fw_parent_device(unit);
	port->addr = addr;
	port->fill = fill;
	port->idling = true;
	port->next_ktime = 0;
	port->error = false;

	INIT_WORK(&port->work, midi_port_work);

	return 0;
}
EXPORT_SYMBOL(snd_fw_async_midi_port_init);

/**
 * snd_fw_async_midi_port_destroy - free asynchronous MIDI port structure
 * @port: the asynchronous MIDI port structure
 */
void snd_fw_async_midi_port_destroy(struct snd_fw_async_midi_port *port)
{
	snd_fw_async_midi_port_finish(port);
	cancel_work_sync(&port->work);
	kfree(port->buf);
}
EXPORT_SYMBOL(snd_fw_async_midi_port_destroy);

MODULE_DESCRIPTION("FireWire audio helper functions");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");
