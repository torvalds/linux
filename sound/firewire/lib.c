// SPDX-License-Identifier: GPL-2.0-only
/*
 * miscellaneous helper functions
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
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

MODULE_DESCRIPTION("FireWire audio helper functions");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");
