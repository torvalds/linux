/*
 * Copyright (C) 2006 - 2007 Ivo van Doorn
 * Copyright (C) 2007 Dmitry Torokhov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/capability.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rfkill.h>

/* Get declaration of rfkill_switch_all() to shut up sparse. */
#include "rfkill-input.h"


MODULE_AUTHOR("Ivo van Doorn <IvDoorn@gmail.com>");
MODULE_VERSION("1.0");
MODULE_DESCRIPTION("RF switch support");
MODULE_LICENSE("GPL");

static LIST_HEAD(rfkill_list);	/* list of registered rf switches */
static DEFINE_MUTEX(rfkill_mutex);

static unsigned int rfkill_default_state = RFKILL_STATE_UNBLOCKED;
module_param_named(default_state, rfkill_default_state, uint, 0444);
MODULE_PARM_DESC(default_state,
		 "Default initial state for all radio types, 0 = radio off");

struct rfkill_gsw_state {
	enum rfkill_state current_state;
	enum rfkill_state default_state;
};

static struct rfkill_gsw_state rfkill_global_states[RFKILL_TYPE_MAX];
static unsigned long rfkill_states_lockdflt[BITS_TO_LONGS(RFKILL_TYPE_MAX)];

static BLOCKING_NOTIFIER_HEAD(rfkill_notifier_list);


/**
 * register_rfkill_notifier - Add notifier to rfkill notifier chain
 * @nb: pointer to the new entry to add to the chain
 *
 * See blocking_notifier_chain_register() for return value and further
 * observations.
 *
 * Adds a notifier to the rfkill notifier chain.  The chain will be
 * called with a pointer to the relevant rfkill structure as a parameter,
 * refer to include/linux/rfkill.h for the possible events.
 *
 * Notifiers added to this chain are to always return NOTIFY_DONE.  This
 * chain is a blocking notifier chain: notifiers can sleep.
 *
 * Calls to this chain may have been done through a workqueue.  One must
 * assume unordered asynchronous behaviour, there is no way to know if
 * actions related to the event that generated the notification have been
 * carried out already.
 */
int register_rfkill_notifier(struct notifier_block *nb)
{
	BUG_ON(!nb);
	return blocking_notifier_chain_register(&rfkill_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(register_rfkill_notifier);

/**
 * unregister_rfkill_notifier - remove notifier from rfkill notifier chain
 * @nb: pointer to the entry to remove from the chain
 *
 * See blocking_notifier_chain_unregister() for return value and further
 * observations.
 *
 * Removes a notifier from the rfkill notifier chain.
 */
int unregister_rfkill_notifier(struct notifier_block *nb)
{
	BUG_ON(!nb);
	return blocking_notifier_chain_unregister(&rfkill_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_rfkill_notifier);


static void rfkill_led_trigger(struct rfkill *rfkill,
			       enum rfkill_state state)
{
#ifdef CONFIG_RFKILL_LEDS
	struct led_trigger *led = &rfkill->led_trigger;

	if (!led->name)
		return;
	if (state != RFKILL_STATE_UNBLOCKED)
		led_trigger_event(led, LED_OFF);
	else
		led_trigger_event(led, LED_FULL);
#endif /* CONFIG_RFKILL_LEDS */
}

#ifdef CONFIG_RFKILL_LEDS
static void rfkill_led_trigger_activate(struct led_classdev *led)
{
	struct rfkill *rfkill = container_of(led->trigger,
			struct rfkill, led_trigger);

	rfkill_led_trigger(rfkill, rfkill->state);
}
#endif /* CONFIG_RFKILL_LEDS */

static void notify_rfkill_state_change(struct rfkill *rfkill)
{
	blocking_notifier_call_chain(&rfkill_notifier_list,
			RFKILL_STATE_CHANGED,
			rfkill);
}

static void update_rfkill_state(struct rfkill *rfkill)
{
	enum rfkill_state newstate, oldstate;

	if (rfkill->get_state) {
		mutex_lock(&rfkill->mutex);
		if (!rfkill->get_state(rfkill->data, &newstate)) {
			oldstate = rfkill->state;
			rfkill->state = newstate;
			if (oldstate != newstate)
				notify_rfkill_state_change(rfkill);
		}
		mutex_unlock(&rfkill->mutex);
	}
}

/**
 * rfkill_toggle_radio - wrapper for toggle_radio hook
 * @rfkill: the rfkill struct to use
 * @force: calls toggle_radio even if cache says it is not needed,
 *	and also makes sure notifications of the state will be
 *	sent even if it didn't change
 * @state: the new state to call toggle_radio() with
 *
 * Calls rfkill->toggle_radio, enforcing the API for toggle_radio
 * calls and handling all the red tape such as issuing notifications
 * if the call is successful.
 *
 * Suspended devices are not touched at all, and -EAGAIN is returned.
 *
 * Note that the @force parameter cannot override a (possibly cached)
 * state of RFKILL_STATE_HARD_BLOCKED.  Any device making use of
 * RFKILL_STATE_HARD_BLOCKED implements either get_state() or
 * rfkill_force_state(), so the cache either is bypassed or valid.
 *
 * Note that we do call toggle_radio for RFKILL_STATE_SOFT_BLOCKED
 * even if the radio is in RFKILL_STATE_HARD_BLOCKED state, so as to
 * give the driver a hint that it should double-BLOCK the transmitter.
 *
 * Caller must have acquired rfkill->mutex.
 */
static int rfkill_toggle_radio(struct rfkill *rfkill,
				enum rfkill_state state,
				int force)
{
	int retval = 0;
	enum rfkill_state oldstate, newstate;

	if (unlikely(rfkill->dev.power.power_state.event & PM_EVENT_SLEEP))
		return -EBUSY;

	oldstate = rfkill->state;

	if (rfkill->get_state && !force &&
	    !rfkill->get_state(rfkill->data, &newstate))
		rfkill->state = newstate;

	switch (state) {
	case RFKILL_STATE_HARD_BLOCKED:
		/* typically happens when refreshing hardware state,
		 * such as on resume */
		state = RFKILL_STATE_SOFT_BLOCKED;
		break;
	case RFKILL_STATE_UNBLOCKED:
		/* force can't override this, only rfkill_force_state() can */
		if (rfkill->state == RFKILL_STATE_HARD_BLOCKED)
			return -EPERM;
		break;
	case RFKILL_STATE_SOFT_BLOCKED:
		/* nothing to do, we want to give drivers the hint to double
		 * BLOCK even a transmitter that is already in state
		 * RFKILL_STATE_HARD_BLOCKED */
		break;
	default:
		WARN(1, KERN_WARNING
			"rfkill: illegal state %d passed as parameter "
			"to rfkill_toggle_radio\n", state);
		return -EINVAL;
	}

	if (force || state != rfkill->state) {
		retval = rfkill->toggle_radio(rfkill->data, state);
		/* never allow a HARD->SOFT downgrade! */
		if (!retval && rfkill->state != RFKILL_STATE_HARD_BLOCKED)
			rfkill->state = state;
	}

	if (force || rfkill->state != oldstate) {
		rfkill_led_trigger(rfkill, rfkill->state);
		notify_rfkill_state_change(rfkill);
	}

	return retval;
}

/**
 * __rfkill_switch_all - Toggle state of all switches of given type
 * @type: type of interfaces to be affected
 * @state: the new state
 *
 * This function toggles the state of all switches of given type,
 * unless a specific switch is claimed by userspace (in which case,
 * that switch is left alone) or suspended.
 *
 * Caller must have acquired rfkill_mutex.
 */
static void __rfkill_switch_all(const enum rfkill_type type,
				const enum rfkill_state state)
{
	struct rfkill *rfkill;

	if (WARN((state >= RFKILL_STATE_MAX || type >= RFKILL_TYPE_MAX),
			KERN_WARNING
			"rfkill: illegal state %d or type %d "
			"passed as parameter to __rfkill_switch_all\n",
			state, type))
		return;

	rfkill_global_states[type].current_state = state;
	list_for_each_entry(rfkill, &rfkill_list, node) {
		if ((!rfkill->user_claim) && (rfkill->type == type)) {
			mutex_lock(&rfkill->mutex);
			rfkill_toggle_radio(rfkill, state, 0);
			mutex_unlock(&rfkill->mutex);
		}
	}
}

/**
 * rfkill_switch_all - Toggle state of all switches of given type
 * @type: type of interfaces to be affected
 * @state: the new state
 *
 * Acquires rfkill_mutex and calls __rfkill_switch_all(@type, @state).
 * Please refer to __rfkill_switch_all() for details.
 */
void rfkill_switch_all(enum rfkill_type type, enum rfkill_state state)
{
	mutex_lock(&rfkill_mutex);
	__rfkill_switch_all(type, state);
	mutex_unlock(&rfkill_mutex);
}
EXPORT_SYMBOL(rfkill_switch_all);

/**
 * rfkill_epo - emergency power off all transmitters
 *
 * This kicks all non-suspended rfkill devices to RFKILL_STATE_SOFT_BLOCKED,
 * ignoring everything in its path but rfkill_mutex and rfkill->mutex.
 *
 * The global state before the EPO is saved and can be restored later
 * using rfkill_restore_states().
 */
void rfkill_epo(void)
{
	struct rfkill *rfkill;
	int i;

	mutex_lock(&rfkill_mutex);
	list_for_each_entry(rfkill, &rfkill_list, node) {
		mutex_lock(&rfkill->mutex);
		rfkill_toggle_radio(rfkill, RFKILL_STATE_SOFT_BLOCKED, 1);
		mutex_unlock(&rfkill->mutex);
	}
	for (i = 0; i < RFKILL_TYPE_MAX; i++) {
		rfkill_global_states[i].default_state =
				rfkill_global_states[i].current_state;
		rfkill_global_states[i].current_state =
				RFKILL_STATE_SOFT_BLOCKED;
	}
	mutex_unlock(&rfkill_mutex);
}
EXPORT_SYMBOL_GPL(rfkill_epo);

/**
 * rfkill_restore_states - restore global states
 *
 * Restore (and sync switches to) the global state from the
 * states in rfkill_default_states.  This can undo the effects of
 * a call to rfkill_epo().
 */
void rfkill_restore_states(void)
{
	int i;

	mutex_lock(&rfkill_mutex);
	for (i = 0; i < RFKILL_TYPE_MAX; i++)
		__rfkill_switch_all(i, rfkill_global_states[i].default_state);
	mutex_unlock(&rfkill_mutex);
}
EXPORT_SYMBOL_GPL(rfkill_restore_states);

/**
 * rfkill_force_state - Force the internal rfkill radio state
 * @rfkill: pointer to the rfkill class to modify.
 * @state: the current radio state the class should be forced to.
 *
 * This function updates the internal state of the radio cached
 * by the rfkill class.  It should be used when the driver gets
 * a notification by the firmware/hardware of the current *real*
 * state of the radio rfkill switch.
 *
 * Devices which are subject to external changes on their rfkill
 * state (such as those caused by a hardware rfkill line) MUST
 * have their driver arrange to call rfkill_force_state() as soon
 * as possible after such a change.
 *
 * This function may not be called from an atomic context.
 */
int rfkill_force_state(struct rfkill *rfkill, enum rfkill_state state)
{
	enum rfkill_state oldstate;

	BUG_ON(!rfkill);
	if (WARN((state >= RFKILL_STATE_MAX),
			KERN_WARNING
			"rfkill: illegal state %d passed as parameter "
			"to rfkill_force_state\n", state))
		return -EINVAL;

	mutex_lock(&rfkill->mutex);

	oldstate = rfkill->state;
	rfkill->state = state;

	if (state != oldstate)
		notify_rfkill_state_change(rfkill);

	mutex_unlock(&rfkill->mutex);

	return 0;
}
EXPORT_SYMBOL(rfkill_force_state);

static ssize_t rfkill_name_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%s\n", rfkill->name);
}

static const char *rfkill_get_type_str(enum rfkill_type type)
{
	switch (type) {
	case RFKILL_TYPE_WLAN:
		return "wlan";
	case RFKILL_TYPE_BLUETOOTH:
		return "bluetooth";
	case RFKILL_TYPE_UWB:
		return "ultrawideband";
	case RFKILL_TYPE_WIMAX:
		return "wimax";
	case RFKILL_TYPE_WWAN:
		return "wwan";
	default:
		BUG();
	}
}

static ssize_t rfkill_type_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%s\n", rfkill_get_type_str(rfkill->type));
}

static ssize_t rfkill_state_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	update_rfkill_state(rfkill);
	return sprintf(buf, "%d\n", rfkill->state);
}

static ssize_t rfkill_state_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct rfkill *rfkill = to_rfkill(dev);
	unsigned long state;
	int error;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	error = strict_strtoul(buf, 0, &state);
	if (error)
		return error;

	/* RFKILL_STATE_HARD_BLOCKED is illegal here... */
	if (state != RFKILL_STATE_UNBLOCKED &&
	    state != RFKILL_STATE_SOFT_BLOCKED)
		return -EINVAL;

	if (mutex_lock_interruptible(&rfkill->mutex))
		return -ERESTARTSYS;
	error = rfkill_toggle_radio(rfkill, state, 0);
	mutex_unlock(&rfkill->mutex);

	return error ? error : count;
}

static ssize_t rfkill_claim_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%d\n", rfkill->user_claim);
}

static ssize_t rfkill_claim_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct rfkill *rfkill = to_rfkill(dev);
	unsigned long claim_tmp;
	bool claim;
	int error;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (rfkill->user_claim_unsupported)
		return -EOPNOTSUPP;

	error = strict_strtoul(buf, 0, &claim_tmp);
	if (error)
		return error;
	claim = !!claim_tmp;

	/*
	 * Take the global lock to make sure the kernel is not in
	 * the middle of rfkill_switch_all
	 */
	error = mutex_lock_interruptible(&rfkill_mutex);
	if (error)
		return error;

	if (rfkill->user_claim != claim) {
		if (!claim) {
			mutex_lock(&rfkill->mutex);
			rfkill_toggle_radio(rfkill,
					rfkill_global_states[rfkill->type].current_state,
					0);
			mutex_unlock(&rfkill->mutex);
		}
		rfkill->user_claim = claim;
	}

	mutex_unlock(&rfkill_mutex);

	return error ? error : count;
}

static struct device_attribute rfkill_dev_attrs[] = {
	__ATTR(name, S_IRUGO, rfkill_name_show, NULL),
	__ATTR(type, S_IRUGO, rfkill_type_show, NULL),
	__ATTR(state, S_IRUGO|S_IWUSR, rfkill_state_show, rfkill_state_store),
	__ATTR(claim, S_IRUGO|S_IWUSR, rfkill_claim_show, rfkill_claim_store),
	__ATTR_NULL
};

static void rfkill_release(struct device *dev)
{
	struct rfkill *rfkill = to_rfkill(dev);

	kfree(rfkill);
	module_put(THIS_MODULE);
}

#ifdef CONFIG_PM
static int rfkill_suspend(struct device *dev, pm_message_t state)
{
	struct rfkill *rfkill = to_rfkill(dev);

	if (dev->power.power_state.event != state.event) {
		if (state.event & PM_EVENT_SLEEP) {
			/* Stop transmitter, keep state, no notifies */
			update_rfkill_state(rfkill);

			mutex_lock(&rfkill->mutex);
			rfkill->toggle_radio(rfkill->data,
						RFKILL_STATE_SOFT_BLOCKED);
			mutex_unlock(&rfkill->mutex);
		}

		dev->power.power_state = state;
	}

	return 0;
}

static int rfkill_resume(struct device *dev)
{
	struct rfkill *rfkill = to_rfkill(dev);

	if (dev->power.power_state.event != PM_EVENT_ON) {
		mutex_lock(&rfkill->mutex);

		dev->power.power_state.event = PM_EVENT_ON;

		/* restore radio state AND notify everybody */
		rfkill_toggle_radio(rfkill, rfkill->state, 1);

		mutex_unlock(&rfkill->mutex);
	}

	return 0;
}
#else
#define rfkill_suspend NULL
#define rfkill_resume NULL
#endif

static int rfkill_blocking_uevent_notifier(struct notifier_block *nb,
					unsigned long eventid,
					void *data)
{
	struct rfkill *rfkill = (struct rfkill *)data;

	switch (eventid) {
	case RFKILL_STATE_CHANGED:
		kobject_uevent(&rfkill->dev.kobj, KOBJ_CHANGE);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block rfkill_blocking_uevent_nb = {
	.notifier_call	= rfkill_blocking_uevent_notifier,
	.priority	= 0,
};

static int rfkill_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct rfkill *rfkill = to_rfkill(dev);
	int error;

	error = add_uevent_var(env, "RFKILL_NAME=%s", rfkill->name);
	if (error)
		return error;
	error = add_uevent_var(env, "RFKILL_TYPE=%s",
				rfkill_get_type_str(rfkill->type));
	if (error)
		return error;
	error = add_uevent_var(env, "RFKILL_STATE=%d", rfkill->state);
	return error;
}

static struct class rfkill_class = {
	.name		= "rfkill",
	.dev_release	= rfkill_release,
	.dev_attrs	= rfkill_dev_attrs,
	.suspend	= rfkill_suspend,
	.resume		= rfkill_resume,
	.dev_uevent	= rfkill_dev_uevent,
};

static int rfkill_check_duplicity(const struct rfkill *rfkill)
{
	struct rfkill *p;
	unsigned long seen[BITS_TO_LONGS(RFKILL_TYPE_MAX)];

	memset(seen, 0, sizeof(seen));

	list_for_each_entry(p, &rfkill_list, node) {
		if (WARN((p == rfkill), KERN_WARNING
				"rfkill: illegal attempt to register "
				"an already registered rfkill struct\n"))
			return -EEXIST;
		set_bit(p->type, seen);
	}

	/* 0: first switch of its kind */
	return test_bit(rfkill->type, seen);
}

static int rfkill_add_switch(struct rfkill *rfkill)
{
	int error;

	mutex_lock(&rfkill_mutex);

	error = rfkill_check_duplicity(rfkill);
	if (error < 0)
		goto unlock_out;

	if (!error) {
		/* lock default after first use */
		set_bit(rfkill->type, rfkill_states_lockdflt);
		rfkill_global_states[rfkill->type].current_state =
			rfkill_global_states[rfkill->type].default_state;
	}

	rfkill_toggle_radio(rfkill,
			    rfkill_global_states[rfkill->type].current_state,
			    0);

	list_add_tail(&rfkill->node, &rfkill_list);

	error = 0;
unlock_out:
	mutex_unlock(&rfkill_mutex);

	return error;
}

static void rfkill_remove_switch(struct rfkill *rfkill)
{
	mutex_lock(&rfkill_mutex);
	list_del_init(&rfkill->node);
	mutex_unlock(&rfkill_mutex);

	mutex_lock(&rfkill->mutex);
	rfkill_toggle_radio(rfkill, RFKILL_STATE_SOFT_BLOCKED, 1);
	mutex_unlock(&rfkill->mutex);
}

/**
 * rfkill_allocate - allocate memory for rfkill structure.
 * @parent: device that has rf switch on it
 * @type: type of the switch (RFKILL_TYPE_*)
 *
 * This function should be called by the network driver when it needs
 * rfkill structure.  Once the structure is allocated the driver should
 * finish its initialization by setting the name, private data, enable_radio
 * and disable_radio methods and then register it with rfkill_register().
 *
 * NOTE: If registration fails the structure shoudl be freed by calling
 * rfkill_free() otherwise rfkill_unregister() should be used.
 */
struct rfkill * __must_check rfkill_allocate(struct device *parent,
					     enum rfkill_type type)
{
	struct rfkill *rfkill;
	struct device *dev;

	if (WARN((type >= RFKILL_TYPE_MAX),
			KERN_WARNING
			"rfkill: illegal type %d passed as parameter "
			"to rfkill_allocate\n", type))
		return NULL;

	rfkill = kzalloc(sizeof(struct rfkill), GFP_KERNEL);
	if (!rfkill)
		return NULL;

	mutex_init(&rfkill->mutex);
	INIT_LIST_HEAD(&rfkill->node);
	rfkill->type = type;

	dev = &rfkill->dev;
	dev->class = &rfkill_class;
	dev->parent = parent;
	device_initialize(dev);

	__module_get(THIS_MODULE);

	return rfkill;
}
EXPORT_SYMBOL(rfkill_allocate);

/**
 * rfkill_free - Mark rfkill structure for deletion
 * @rfkill: rfkill structure to be destroyed
 *
 * Decrements reference count of the rfkill structure so it is destroyed.
 * Note that rfkill_free() should _not_ be called after rfkill_unregister().
 */
void rfkill_free(struct rfkill *rfkill)
{
	if (rfkill)
		put_device(&rfkill->dev);
}
EXPORT_SYMBOL(rfkill_free);

static void rfkill_led_trigger_register(struct rfkill *rfkill)
{
#ifdef CONFIG_RFKILL_LEDS
	int error;

	if (!rfkill->led_trigger.name)
		rfkill->led_trigger.name = rfkill->dev.bus_id;
	if (!rfkill->led_trigger.activate)
		rfkill->led_trigger.activate = rfkill_led_trigger_activate;
	error = led_trigger_register(&rfkill->led_trigger);
	if (error)
		rfkill->led_trigger.name = NULL;
#endif /* CONFIG_RFKILL_LEDS */
}

static void rfkill_led_trigger_unregister(struct rfkill *rfkill)
{
#ifdef CONFIG_RFKILL_LEDS
	if (rfkill->led_trigger.name) {
		led_trigger_unregister(&rfkill->led_trigger);
		rfkill->led_trigger.name = NULL;
	}
#endif
}

/**
 * rfkill_register - Register a rfkill structure.
 * @rfkill: rfkill structure to be registered
 *
 * This function should be called by the network driver when the rfkill
 * structure needs to be registered. Immediately from registration the
 * switch driver should be able to service calls to toggle_radio.
 */
int __must_check rfkill_register(struct rfkill *rfkill)
{
	static atomic_t rfkill_no = ATOMIC_INIT(0);
	struct device *dev = &rfkill->dev;
	int error;

	if (WARN((!rfkill || !rfkill->toggle_radio ||
			rfkill->type >= RFKILL_TYPE_MAX ||
			rfkill->state >= RFKILL_STATE_MAX),
			KERN_WARNING
			"rfkill: attempt to register a "
			"badly initialized rfkill struct\n"))
		return -EINVAL;

	snprintf(dev->bus_id, sizeof(dev->bus_id),
		 "rfkill%ld", (long)atomic_inc_return(&rfkill_no) - 1);

	rfkill_led_trigger_register(rfkill);

	error = rfkill_add_switch(rfkill);
	if (error) {
		rfkill_led_trigger_unregister(rfkill);
		return error;
	}

	error = device_add(dev);
	if (error) {
		rfkill_remove_switch(rfkill);
		rfkill_led_trigger_unregister(rfkill);
		return error;
	}

	return 0;
}
EXPORT_SYMBOL(rfkill_register);

/**
 * rfkill_unregister - Unregister a rfkill structure.
 * @rfkill: rfkill structure to be unregistered
 *
 * This function should be called by the network driver during device
 * teardown to destroy rfkill structure. Note that rfkill_free() should
 * _not_ be called after rfkill_unregister().
 */
void rfkill_unregister(struct rfkill *rfkill)
{
	BUG_ON(!rfkill);
	device_del(&rfkill->dev);
	rfkill_remove_switch(rfkill);
	rfkill_led_trigger_unregister(rfkill);
	put_device(&rfkill->dev);
}
EXPORT_SYMBOL(rfkill_unregister);

/**
 * rfkill_set_default - set initial value for a switch type
 * @type - the type of switch to set the default state of
 * @state - the new default state for that group of switches
 *
 * Sets the initial state rfkill should use for a given type.
 * The following initial states are allowed: RFKILL_STATE_SOFT_BLOCKED
 * and RFKILL_STATE_UNBLOCKED.
 *
 * This function is meant to be used by platform drivers for platforms
 * that can save switch state across power down/reboot.
 *
 * The default state for each switch type can be changed exactly once.
 * After a switch of that type is registered, the default state cannot
 * be changed anymore.  This guards against multiple drivers it the
 * same platform trying to set the initial switch default state, which
 * is not allowed.
 *
 * Returns -EPERM if the state has already been set once or is in use,
 * so drivers likely want to either ignore or at most printk(KERN_NOTICE)
 * if this function returns -EPERM.
 *
 * Returns 0 if the new default state was set, or an error if it
 * could not be set.
 */
int rfkill_set_default(enum rfkill_type type, enum rfkill_state state)
{
	int error;

	if (WARN((type >= RFKILL_TYPE_MAX ||
			(state != RFKILL_STATE_SOFT_BLOCKED &&
			 state != RFKILL_STATE_UNBLOCKED)),
			KERN_WARNING
			"rfkill: illegal state %d or type %d passed as "
			"parameter to rfkill_set_default\n", state, type))
		return -EINVAL;

	mutex_lock(&rfkill_mutex);

	if (!test_and_set_bit(type, rfkill_states_lockdflt)) {
		rfkill_global_states[type].default_state = state;
		error = 0;
	} else
		error = -EPERM;

	mutex_unlock(&rfkill_mutex);
	return error;
}
EXPORT_SYMBOL_GPL(rfkill_set_default);

/*
 * Rfkill module initialization/deinitialization.
 */
static int __init rfkill_init(void)
{
	int error;
	int i;

	/* RFKILL_STATE_HARD_BLOCKED is illegal here... */
	if (rfkill_default_state != RFKILL_STATE_SOFT_BLOCKED &&
	    rfkill_default_state != RFKILL_STATE_UNBLOCKED)
		return -EINVAL;

	for (i = 0; i < RFKILL_TYPE_MAX; i++)
		rfkill_global_states[i].default_state = rfkill_default_state;

	error = class_register(&rfkill_class);
	if (error) {
		printk(KERN_ERR "rfkill: unable to register rfkill class\n");
		return error;
	}

	register_rfkill_notifier(&rfkill_blocking_uevent_nb);

	return 0;
}

static void __exit rfkill_exit(void)
{
	unregister_rfkill_notifier(&rfkill_blocking_uevent_nb);
	class_unregister(&rfkill_class);
}

subsys_initcall(rfkill_init);
module_exit(rfkill_exit);
