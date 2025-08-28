// SPDX-License-Identifier: GPL-2.0-only
/*
 * isochronous resources helper functions
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include "iso-resources.h"

/**
 * fw_iso_resources_init - initializes a &struct fw_iso_resources
 * @r: the resource manager to initialize
 * @unit: the device unit for which the resources will be needed
 *
 * If the device does not support all channel numbers, change @r->channels_mask
 * after calling this function.
 */
int fw_iso_resources_init(struct fw_iso_resources *r, struct fw_unit *unit)
{
	r->channels_mask = ~0uLL;
	r->unit = unit;
	mutex_init(&r->mutex);
	r->allocated = false;

	return 0;
}
EXPORT_SYMBOL(fw_iso_resources_init);

/**
 * fw_iso_resources_destroy - destroy a resource manager
 * @r: the resource manager that is no longer needed
 */
void fw_iso_resources_destroy(struct fw_iso_resources *r)
{
	WARN_ON(r->allocated);
	mutex_destroy(&r->mutex);
}
EXPORT_SYMBOL(fw_iso_resources_destroy);

static unsigned int packet_bandwidth(unsigned int max_payload_bytes, int speed)
{
	unsigned int bytes, s400_bytes;

	/* iso packets have three header quadlets and quadlet-aligned payload */
	bytes = 3 * 4 + ALIGN(max_payload_bytes, 4);

	/* convert to bandwidth units (quadlets at S1600 = bytes at S400) */
	if (speed <= SCODE_400)
		s400_bytes = bytes * (1 << (SCODE_400 - speed));
	else
		s400_bytes = DIV_ROUND_UP(bytes, 1 << (speed - SCODE_400));

	return s400_bytes;
}

static int current_bandwidth_overhead(struct fw_card *card)
{
	/*
	 * Under the usual pessimistic assumption (cable length 4.5 m), the
	 * isochronous overhead for N cables is 1.797 µs + N * 0.494 µs, or
	 * 88.3 + N * 24.3 in bandwidth units.
	 *
	 * The calculation below tries to deduce N from the current gap count.
	 * If the gap count has been optimized by measuring the actual packet
	 * transmission time, this derived overhead should be near the actual
	 * overhead as well.
	 */
	return card->gap_count < 63 ? card->gap_count * 97 / 10 + 89 : 512;
}

static int wait_isoch_resource_delay_after_bus_reset(struct fw_card *card)
{
	for (;;) {
		s64 delay = (card->reset_jiffies + HZ) - get_jiffies_64();
		if (delay <= 0)
			return 0;
		if (schedule_timeout_interruptible(delay) > 0)
			return -ERESTARTSYS;
	}
}

/**
 * fw_iso_resources_allocate - allocate isochronous channel and bandwidth
 * @r: the resource manager
 * @max_payload_bytes: the amount of data (including CIP headers) per packet
 * @speed: the speed (e.g., SCODE_400) at which the packets will be sent
 *
 * This function allocates one isochronous channel and enough bandwidth for the
 * specified packet size.
 *
 * Returns the channel number that the caller must use for streaming, or
 * a negative error code.  Due to potentionally long delays, this function is
 * interruptible and can return -ERESTARTSYS.  On success, the caller is
 * responsible for calling fw_iso_resources_update() on bus resets, and
 * fw_iso_resources_free() when the resources are not longer needed.
 */
int fw_iso_resources_allocate(struct fw_iso_resources *r,
			      unsigned int max_payload_bytes, int speed)
{
	struct fw_card *card = fw_parent_device(r->unit)->card;
	int bandwidth, channel, err;

	if (WARN_ON(r->allocated))
		return -EBADFD;

	r->bandwidth = packet_bandwidth(max_payload_bytes, speed);

retry_after_bus_reset:
	scoped_guard(spinlock_irq, &card->lock) {
		r->generation = card->generation;
		r->bandwidth_overhead = current_bandwidth_overhead(card);
	}

	err = wait_isoch_resource_delay_after_bus_reset(card);
	if (err < 0)
		return err;

	scoped_guard(mutex, &r->mutex) {
		bandwidth = r->bandwidth + r->bandwidth_overhead;
		fw_iso_resource_manage(card, r->generation, r->channels_mask,
				       &channel, &bandwidth, true);
		if (channel == -EAGAIN)
			goto retry_after_bus_reset;
		if (channel >= 0) {
			r->channel = channel;
			r->allocated = true;
		} else {
			if (channel == -EBUSY)
				dev_err(&r->unit->device,
					"isochronous resources exhausted\n");
			else
				dev_err(&r->unit->device,
					"isochronous resource allocation failed\n");
		}
	}

	return channel;
}
EXPORT_SYMBOL(fw_iso_resources_allocate);

/**
 * fw_iso_resources_update - update resource allocations after a bus reset
 * @r: the resource manager
 *
 * This function must be called from the driver's .update handler to reallocate
 * any resources that were allocated before the bus reset.  It is safe to call
 * this function if no resources are currently allocated.
 *
 * Returns a negative error code on failure.  If this happens, the caller must
 * stop streaming.
 */
int fw_iso_resources_update(struct fw_iso_resources *r)
{
	struct fw_card *card = fw_parent_device(r->unit)->card;
	int bandwidth, channel;

	guard(mutex)(&r->mutex);

	if (!r->allocated)
		return 0;

	scoped_guard(spinlock_irq, &card->lock) {
		r->generation = card->generation;
		r->bandwidth_overhead = current_bandwidth_overhead(card);
	}

	bandwidth = r->bandwidth + r->bandwidth_overhead;

	fw_iso_resource_manage(card, r->generation, 1uLL << r->channel,
			       &channel, &bandwidth, true);
	/*
	 * When another bus reset happens, pretend that the allocation
	 * succeeded; we will try again for the new generation later.
	 */
	if (channel < 0 && channel != -EAGAIN) {
		r->allocated = false;
		if (channel == -EBUSY)
			dev_err(&r->unit->device,
				"isochronous resources exhausted\n");
		else
			dev_err(&r->unit->device,
				"isochronous resource allocation failed\n");
	}

	return channel;
}
EXPORT_SYMBOL(fw_iso_resources_update);

/**
 * fw_iso_resources_free - frees allocated resources
 * @r: the resource manager
 *
 * This function deallocates the channel and bandwidth, if allocated.
 */
void fw_iso_resources_free(struct fw_iso_resources *r)
{
	struct fw_card *card;
	int bandwidth, channel;

	/* Not initialized. */
	if (r->unit == NULL)
		return;
	card = fw_parent_device(r->unit)->card;

	guard(mutex)(&r->mutex);

	if (r->allocated) {
		bandwidth = r->bandwidth + r->bandwidth_overhead;
		fw_iso_resource_manage(card, r->generation, 1uLL << r->channel,
				       &channel, &bandwidth, false);
		if (channel < 0)
			dev_err(&r->unit->device,
				"isochronous resource deallocation failed\n");

		r->allocated = false;
	}
}
EXPORT_SYMBOL(fw_iso_resources_free);
