// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input layer to RF Kill interface connector
 *
 * Copyright (c) 2007 Dmitry Torokhov
 * Copyright 2009 Johannes Berg <johannes@sipsolutions.net>
 *
 * If you ever run into a situation in which you have a SW_ type rfkill
 * input device, then you can revive code that was removed in the patch
 * "rfkill-input: remove unused code".
 */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/init.h>
#include <linux/rfkill.h>
#include <linux/sched.h>

#include "rfkill.h"

enum rfkill_input_master_mode {
	RFKILL_INPUT_MASTER_UNLOCK = 0,
	RFKILL_INPUT_MASTER_RESTORE = 1,
	RFKILL_INPUT_MASTER_UNBLOCKALL = 2,
	NUM_RFKILL_INPUT_MASTER_MODES
};

/* Delay (in ms) between consecutive switch ops */
#define RFKILL_OPS_DELAY 200

static enum rfkill_input_master_mode rfkill_master_switch_mode =
					RFKILL_INPUT_MASTER_UNBLOCKALL;
module_param_named(master_switch_mode, rfkill_master_switch_mode, uint, 0);
MODULE_PARM_DESC(master_switch_mode,
	"SW_RFKILL_ALL ON should: 0=do nothing (only unlock); 1=restore; 2=unblock all");

static spinlock_t rfkill_op_lock;
static bool rfkill_op_pending;
static unsigned long rfkill_sw_pending[BITS_TO_LONGS(NUM_RFKILL_TYPES)];
static unsigned long rfkill_sw_state[BITS_TO_LONGS(NUM_RFKILL_TYPES)];

enum rfkill_sched_op {
	RFKILL_GLOBAL_OP_EPO = 0,
	RFKILL_GLOBAL_OP_RESTORE,
	RFKILL_GLOBAL_OP_UNLOCK,
	RFKILL_GLOBAL_OP_UNBLOCK,
};

static enum rfkill_sched_op rfkill_master_switch_op;
static enum rfkill_sched_op rfkill_op;

static void __rfkill_handle_global_op(enum rfkill_sched_op op)
{
	unsigned int i;

	switch (op) {
	case RFKILL_GLOBAL_OP_EPO:
		rfkill_epo();
		break;
	case RFKILL_GLOBAL_OP_RESTORE:
		rfkill_restore_states();
		break;
	case RFKILL_GLOBAL_OP_UNLOCK:
		rfkill_remove_epo_lock();
		break;
	case RFKILL_GLOBAL_OP_UNBLOCK:
		rfkill_remove_epo_lock();
		for (i = 0; i < NUM_RFKILL_TYPES; i++)
			rfkill_switch_all(i, false);
		break;
	default:
		/* memory corruption or bug, fail safely */
		rfkill_epo();
		WARN(1, "Unknown requested operation %d! "
			"rfkill Emergency Power Off activated\n",
			op);
	}
}

static void __rfkill_handle_normal_op(const enum rfkill_type type,
				      const bool complement)
{
	bool blocked;

	blocked = rfkill_get_global_sw_state(type);
	if (complement)
		blocked = !blocked;

	rfkill_switch_all(type, blocked);
}

static void rfkill_op_handler(struct work_struct *work)
{
	unsigned int i;
	bool c;

	spin_lock_irq(&rfkill_op_lock);
	do {
		if (rfkill_op_pending) {
			enum rfkill_sched_op op = rfkill_op;
			rfkill_op_pending = false;
			memset(rfkill_sw_pending, 0,
				sizeof(rfkill_sw_pending));
			spin_unlock_irq(&rfkill_op_lock);

			__rfkill_handle_global_op(op);

			spin_lock_irq(&rfkill_op_lock);

			/*
			 * handle global ops first -- during unlocked period
			 * we might have gotten a new global op.
			 */
			if (rfkill_op_pending)
				continue;
		}

		if (rfkill_is_epo_lock_active())
			continue;

		for (i = 0; i < NUM_RFKILL_TYPES; i++) {
			if (__test_and_clear_bit(i, rfkill_sw_pending)) {
				c = __test_and_clear_bit(i, rfkill_sw_state);
				spin_unlock_irq(&rfkill_op_lock);

				__rfkill_handle_normal_op(i, c);

				spin_lock_irq(&rfkill_op_lock);
			}
		}
	} while (rfkill_op_pending);
	spin_unlock_irq(&rfkill_op_lock);
}

static DECLARE_DELAYED_WORK(rfkill_op_work, rfkill_op_handler);
static unsigned long rfkill_last_scheduled;

static unsigned long rfkill_ratelimit(const unsigned long last)
{
	const unsigned long delay = msecs_to_jiffies(RFKILL_OPS_DELAY);
	return time_after(jiffies, last + delay) ? 0 : delay;
}

static void rfkill_schedule_ratelimited(void)
{
	if (schedule_delayed_work(&rfkill_op_work,
				  rfkill_ratelimit(rfkill_last_scheduled)))
		rfkill_last_scheduled = jiffies;
}

static void rfkill_schedule_global_op(enum rfkill_sched_op op)
{
	unsigned long flags;

	spin_lock_irqsave(&rfkill_op_lock, flags);
	rfkill_op = op;
	rfkill_op_pending = true;
	if (op == RFKILL_GLOBAL_OP_EPO && !rfkill_is_epo_lock_active()) {
		/* bypass the limiter for EPO */
		mod_delayed_work(system_wq, &rfkill_op_work, 0);
		rfkill_last_scheduled = jiffies;
	} else
		rfkill_schedule_ratelimited();
	spin_unlock_irqrestore(&rfkill_op_lock, flags);
}

static void rfkill_schedule_toggle(enum rfkill_type type)
{
	unsigned long flags;

	if (rfkill_is_epo_lock_active())
		return;

	spin_lock_irqsave(&rfkill_op_lock, flags);
	if (!rfkill_op_pending) {
		__set_bit(type, rfkill_sw_pending);
		__change_bit(type, rfkill_sw_state);
		rfkill_schedule_ratelimited();
	}
	spin_unlock_irqrestore(&rfkill_op_lock, flags);
}

static void rfkill_schedule_evsw_rfkillall(int state)
{
	if (state)
		rfkill_schedule_global_op(rfkill_master_switch_op);
	else
		rfkill_schedule_global_op(RFKILL_GLOBAL_OP_EPO);
}

static void rfkill_event(struct input_handle *handle, unsigned int type,
			unsigned int code, int data)
{
	if (type == EV_KEY && data == 1) {
		switch (code) {
		case KEY_WLAN:
			rfkill_schedule_toggle(RFKILL_TYPE_WLAN);
			break;
		case KEY_BLUETOOTH:
			rfkill_schedule_toggle(RFKILL_TYPE_BLUETOOTH);
			break;
		case KEY_UWB:
			rfkill_schedule_toggle(RFKILL_TYPE_UWB);
			break;
		case KEY_WIMAX:
			rfkill_schedule_toggle(RFKILL_TYPE_WIMAX);
			break;
		case KEY_RFKILL:
			rfkill_schedule_toggle(RFKILL_TYPE_ALL);
			break;
		}
	} else if (type == EV_SW && code == SW_RFKILL_ALL)
		rfkill_schedule_evsw_rfkillall(data);
}

static int rfkill_connect(struct input_handler *handler, struct input_dev *dev,
			  const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "rfkill";

	/* causes rfkill_start() to be called */
	error = input_register_handle(handle);
	if (error)
		goto err_free_handle;

	error = input_open_device(handle);
	if (error)
		goto err_unregister_handle;

	return 0;

 err_unregister_handle:
	input_unregister_handle(handle);
 err_free_handle:
	kfree(handle);
	return error;
}

static void rfkill_start(struct input_handle *handle)
{
	/*
	 * Take event_lock to guard against configuration changes, we
	 * should be able to deal with concurrency with rfkill_event()
	 * just fine (which event_lock will also avoid).
	 */
	spin_lock_irq(&handle->dev->event_lock);

	if (test_bit(EV_SW, handle->dev->evbit) &&
	    test_bit(SW_RFKILL_ALL, handle->dev->swbit))
		rfkill_schedule_evsw_rfkillall(test_bit(SW_RFKILL_ALL,
							handle->dev->sw));

	spin_unlock_irq(&handle->dev->event_lock);
}

static void rfkill_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id rfkill_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_WLAN)] = BIT_MASK(KEY_WLAN) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_BLUETOOTH)] = BIT_MASK(KEY_BLUETOOTH) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_UWB)] = BIT_MASK(KEY_UWB) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_WIMAX)] = BIT_MASK(KEY_WIMAX) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_RFKILL)] = BIT_MASK(KEY_RFKILL) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_SWBIT,
		.evbit = { BIT(EV_SW) },
		.swbit = { [BIT_WORD(SW_RFKILL_ALL)] = BIT_MASK(SW_RFKILL_ALL) },
	},
	{ }
};

static struct input_handler rfkill_handler = {
	.name =	"rfkill",
	.event = rfkill_event,
	.connect = rfkill_connect,
	.start = rfkill_start,
	.disconnect = rfkill_disconnect,
	.id_table = rfkill_ids,
};

int __init rfkill_handler_init(void)
{
	switch (rfkill_master_switch_mode) {
	case RFKILL_INPUT_MASTER_UNBLOCKALL:
		rfkill_master_switch_op = RFKILL_GLOBAL_OP_UNBLOCK;
		break;
	case RFKILL_INPUT_MASTER_RESTORE:
		rfkill_master_switch_op = RFKILL_GLOBAL_OP_RESTORE;
		break;
	case RFKILL_INPUT_MASTER_UNLOCK:
		rfkill_master_switch_op = RFKILL_GLOBAL_OP_UNLOCK;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_init(&rfkill_op_lock);

	/* Avoid delay at first schedule */
	rfkill_last_scheduled =
			jiffies - msecs_to_jiffies(RFKILL_OPS_DELAY) - 1;
	return input_register_handler(&rfkill_handler);
}

void __exit rfkill_handler_exit(void)
{
	input_unregister_handler(&rfkill_handler);
	cancel_delayed_work_sync(&rfkill_op_work);
}
