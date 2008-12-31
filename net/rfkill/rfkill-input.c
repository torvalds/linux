/*
 * Input layer to RF Kill interface connector
 *
 * Copyright (c) 2007 Dmitry Torokhov
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/init.h>
#include <linux/rfkill.h>
#include <linux/sched.h>

#include "rfkill-input.h"

MODULE_AUTHOR("Dmitry Torokhov <dtor@mail.ru>");
MODULE_DESCRIPTION("Input layer to RF switch connector");
MODULE_LICENSE("GPL");

enum rfkill_input_master_mode {
	RFKILL_INPUT_MASTER_DONOTHING = 0,
	RFKILL_INPUT_MASTER_RESTORE = 1,
	RFKILL_INPUT_MASTER_UNBLOCKALL = 2,
	RFKILL_INPUT_MASTER_MAX,	/* marker */
};

/* Delay (in ms) between consecutive switch ops */
#define RFKILL_OPS_DELAY 200

static enum rfkill_input_master_mode rfkill_master_switch_mode =
					RFKILL_INPUT_MASTER_UNBLOCKALL;
module_param_named(master_switch_mode, rfkill_master_switch_mode, uint, 0);
MODULE_PARM_DESC(master_switch_mode,
	"SW_RFKILL_ALL ON should: 0=do nothing; 1=restore; 2=unblock all");

enum rfkill_global_sched_op {
	RFKILL_GLOBAL_OP_EPO = 0,
	RFKILL_GLOBAL_OP_RESTORE,
	RFKILL_GLOBAL_OP_UNLOCK,
	RFKILL_GLOBAL_OP_UNBLOCK,
};

/*
 * Currently, the code marked with RFKILL_NEED_SWSET is inactive.
 * If handling of EV_SW SW_WLAN/WWAN/BLUETOOTH/etc is needed in the
 * future, when such events are added, that code will be necessary.
 */

struct rfkill_task {
	struct delayed_work dwork;

	/* ensures that task is serialized */
	struct mutex mutex;

	/* protects everything below */
	spinlock_t lock;

	/* pending regular switch operations (1=pending) */
	unsigned long sw_pending[BITS_TO_LONGS(RFKILL_TYPE_MAX)];

#ifdef RFKILL_NEED_SWSET
	/* set operation pending (1=pending) */
	unsigned long sw_setpending[BITS_TO_LONGS(RFKILL_TYPE_MAX)];

	/* desired state for pending set operation (1=unblock) */
	unsigned long sw_newstate[BITS_TO_LONGS(RFKILL_TYPE_MAX)];
#endif

	/* should the state be complemented (1=yes) */
	unsigned long sw_togglestate[BITS_TO_LONGS(RFKILL_TYPE_MAX)];

	bool global_op_pending;
	enum rfkill_global_sched_op op;

	/* last time it was scheduled */
	unsigned long last_scheduled;
};

static void __rfkill_handle_global_op(enum rfkill_global_sched_op op)
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
		for (i = 0; i < RFKILL_TYPE_MAX; i++)
			rfkill_switch_all(i, RFKILL_STATE_UNBLOCKED);
		break;
	default:
		/* memory corruption or bug, fail safely */
		rfkill_epo();
		WARN(1, "Unknown requested operation %d! "
			"rfkill Emergency Power Off activated\n",
			op);
	}
}

#ifdef RFKILL_NEED_SWSET
static void __rfkill_handle_normal_op(const enum rfkill_type type,
			const bool sp, const bool s, const bool c)
{
	enum rfkill_state state;

	if (sp)
		state = (s) ? RFKILL_STATE_UNBLOCKED :
			      RFKILL_STATE_SOFT_BLOCKED;
	else
		state = rfkill_get_global_state(type);

	if (c)
		state = rfkill_state_complement(state);

	rfkill_switch_all(type, state);
}
#else
static void __rfkill_handle_normal_op(const enum rfkill_type type,
			const bool c)
{
	enum rfkill_state state;

	state = rfkill_get_global_state(type);
	if (c)
		state = rfkill_state_complement(state);

	rfkill_switch_all(type, state);
}
#endif

static void rfkill_task_handler(struct work_struct *work)
{
	struct rfkill_task *task = container_of(work,
					struct rfkill_task, dwork.work);
	bool doit = true;

	mutex_lock(&task->mutex);

	spin_lock_irq(&task->lock);
	while (doit) {
		if (task->global_op_pending) {
			enum rfkill_global_sched_op op = task->op;
			task->global_op_pending = false;
			memset(task->sw_pending, 0, sizeof(task->sw_pending));
			spin_unlock_irq(&task->lock);

			__rfkill_handle_global_op(op);

			/* make sure we do at least one pass with
			 * !task->global_op_pending */
			spin_lock_irq(&task->lock);
			continue;
		} else if (!rfkill_is_epo_lock_active()) {
			unsigned int i = 0;

			while (!task->global_op_pending &&
						i < RFKILL_TYPE_MAX) {
				if (test_and_clear_bit(i, task->sw_pending)) {
					bool c;
#ifdef RFKILL_NEED_SWSET
					bool sp, s;
					sp = test_and_clear_bit(i,
							task->sw_setpending);
					s = test_bit(i, task->sw_newstate);
#endif
					c = test_and_clear_bit(i,
							task->sw_togglestate);
					spin_unlock_irq(&task->lock);

#ifdef RFKILL_NEED_SWSET
					__rfkill_handle_normal_op(i, sp, s, c);
#else
					__rfkill_handle_normal_op(i, c);
#endif

					spin_lock_irq(&task->lock);
				}
				i++;
			}
		}
		doit = task->global_op_pending;
	}
	spin_unlock_irq(&task->lock);

	mutex_unlock(&task->mutex);
}

static struct rfkill_task rfkill_task = {
	.dwork = __DELAYED_WORK_INITIALIZER(rfkill_task.dwork,
				rfkill_task_handler),
	.mutex = __MUTEX_INITIALIZER(rfkill_task.mutex),
	.lock = __SPIN_LOCK_UNLOCKED(rfkill_task.lock),
};

static unsigned long rfkill_ratelimit(const unsigned long last)
{
	const unsigned long delay = msecs_to_jiffies(RFKILL_OPS_DELAY);
	return (time_after(jiffies, last + delay)) ? 0 : delay;
}

static void rfkill_schedule_ratelimited(void)
{
	if (!delayed_work_pending(&rfkill_task.dwork)) {
		schedule_delayed_work(&rfkill_task.dwork,
				rfkill_ratelimit(rfkill_task.last_scheduled));
		rfkill_task.last_scheduled = jiffies;
	}
}

static void rfkill_schedule_global_op(enum rfkill_global_sched_op op)
{
	unsigned long flags;

	spin_lock_irqsave(&rfkill_task.lock, flags);
	rfkill_task.op = op;
	rfkill_task.global_op_pending = true;
	if (op == RFKILL_GLOBAL_OP_EPO && !rfkill_is_epo_lock_active()) {
		/* bypass the limiter for EPO */
		cancel_delayed_work(&rfkill_task.dwork);
		schedule_delayed_work(&rfkill_task.dwork, 0);
		rfkill_task.last_scheduled = jiffies;
	} else
		rfkill_schedule_ratelimited();
	spin_unlock_irqrestore(&rfkill_task.lock, flags);
}

#ifdef RFKILL_NEED_SWSET
/* Use this if you need to add EV_SW SW_WLAN/WWAN/BLUETOOTH/etc handling */

static void rfkill_schedule_set(enum rfkill_type type,
				enum rfkill_state desired_state)
{
	unsigned long flags;

	if (rfkill_is_epo_lock_active())
		return;

	spin_lock_irqsave(&rfkill_task.lock, flags);
	if (!rfkill_task.global_op_pending) {
		set_bit(type, rfkill_task.sw_pending);
		set_bit(type, rfkill_task.sw_setpending);
		clear_bit(type, rfkill_task.sw_togglestate);
		if (desired_state)
			set_bit(type,  rfkill_task.sw_newstate);
		else
			clear_bit(type, rfkill_task.sw_newstate);
		rfkill_schedule_ratelimited();
	}
	spin_unlock_irqrestore(&rfkill_task.lock, flags);
}
#endif

static void rfkill_schedule_toggle(enum rfkill_type type)
{
	unsigned long flags;

	if (rfkill_is_epo_lock_active())
		return;

	spin_lock_irqsave(&rfkill_task.lock, flags);
	if (!rfkill_task.global_op_pending) {
		set_bit(type, rfkill_task.sw_pending);
		change_bit(type, rfkill_task.sw_togglestate);
		rfkill_schedule_ratelimited();
	}
	spin_unlock_irqrestore(&rfkill_task.lock, flags);
}

static void rfkill_schedule_evsw_rfkillall(int state)
{
	if (state) {
		switch (rfkill_master_switch_mode) {
		case RFKILL_INPUT_MASTER_UNBLOCKALL:
			rfkill_schedule_global_op(RFKILL_GLOBAL_OP_UNBLOCK);
			break;
		case RFKILL_INPUT_MASTER_RESTORE:
			rfkill_schedule_global_op(RFKILL_GLOBAL_OP_RESTORE);
			break;
		case RFKILL_INPUT_MASTER_DONOTHING:
			rfkill_schedule_global_op(RFKILL_GLOBAL_OP_UNLOCK);
			break;
		default:
			/* memory corruption or driver bug! fail safely */
			rfkill_schedule_global_op(RFKILL_GLOBAL_OP_EPO);
			WARN(1, "Unknown rfkill_master_switch_mode (%d), "
				"driver bug or memory corruption detected!\n",
				rfkill_master_switch_mode);
			break;
		}
	} else
		rfkill_schedule_global_op(RFKILL_GLOBAL_OP_EPO);
}

static void rfkill_event(struct input_handle *handle, unsigned int type,
			unsigned int code, int data)
{
	if (type == EV_KEY && data == 1) {
		enum rfkill_type t;

		switch (code) {
		case KEY_WLAN:
			t = RFKILL_TYPE_WLAN;
			break;
		case KEY_BLUETOOTH:
			t = RFKILL_TYPE_BLUETOOTH;
			break;
		case KEY_UWB:
			t = RFKILL_TYPE_UWB;
			break;
		case KEY_WIMAX:
			t = RFKILL_TYPE_WIMAX;
			break;
		default:
			return;
		}
		rfkill_schedule_toggle(t);
		return;
	} else if (type == EV_SW) {
		switch (code) {
		case SW_RFKILL_ALL:
			rfkill_schedule_evsw_rfkillall(data);
			return;
		default:
			return;
		}
	}
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
	/* Take event_lock to guard against configuration changes, we
	 * should be able to deal with concurrency with rfkill_event()
	 * just fine (which event_lock will also avoid). */
	spin_lock_irq(&handle->dev->event_lock);

	if (test_bit(EV_SW, handle->dev->evbit)) {
		if (test_bit(SW_RFKILL_ALL, handle->dev->swbit))
			rfkill_schedule_evsw_rfkillall(test_bit(SW_RFKILL_ALL,
							handle->dev->sw));
		/* add resync for further EV_SW events here */
	}

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
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_SWBIT,
		.evbit = { BIT(EV_SW) },
		.swbit = { [BIT_WORD(SW_RFKILL_ALL)] = BIT_MASK(SW_RFKILL_ALL) },
	},
	{ }
};

static struct input_handler rfkill_handler = {
	.event =	rfkill_event,
	.connect =	rfkill_connect,
	.disconnect =	rfkill_disconnect,
	.start =	rfkill_start,
	.name =		"rfkill",
	.id_table =	rfkill_ids,
};

static int __init rfkill_handler_init(void)
{
	if (rfkill_master_switch_mode >= RFKILL_INPUT_MASTER_MAX)
		return -EINVAL;

	/*
	 * The penalty to not doing this is a possible RFKILL_OPS_DELAY delay
	 * at the first use.  Acceptable, but if we can avoid it, why not?
	 */
	rfkill_task.last_scheduled =
			jiffies - msecs_to_jiffies(RFKILL_OPS_DELAY) - 1;
	return input_register_handler(&rfkill_handler);
}

static void __exit rfkill_handler_exit(void)
{
	input_unregister_handler(&rfkill_handler);
	cancel_delayed_work_sync(&rfkill_task.dwork);
	rfkill_remove_epo_lock();
}

module_init(rfkill_handler_init);
module_exit(rfkill_handler_exit);
