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

static enum rfkill_state rfkill_states[RFKILL_TYPE_MAX];

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
 * rfkill_switch_all - Toggle state of all switches of given type
 * @type: type of interfaces to be affected
 * @state: the new state
 *
 * This function toggles the state of all switches of given type,
 * unless a specific switch is claimed by userspace (in which case,
 * that switch is left alone) or suspended.
 */
void rfkill_switch_all(enum rfkill_type type, enum rfkill_state state)
{
	struct rfkill *rfkill;

	mutex_lock(&rfkill_mutex);

	rfkill_states[type] = state;

	list_for_each_entry(rfkill, &rfkill_list, node) {
		if ((!rfkill->user_claim) && (rfkill->type == type)) {
			mutex_lock(&rfkill->mutex);
			rfkill_toggle_radio(rfkill, state, 0);
			mutex_unlock(&rfkill->mutex);
		}
	}

	mutex_unlock(&rfkill_mutex);
}
EXPORT_SYMBOL(rfkill_switch_all);

/**
 * rfkill_epo - emergency power off all transmitters
 *
 * This kicks all non-suspended rfkill devices to RFKILL_STATE_SOFT_BLOCKED,
 * ignoring everything in its path but rfkill_mutex and rfkill->mutex.
 */
void rfkill_epo(void)
{
	struct rfkill *rfkill;

	mutex_lock(&rfkill_mutex);
	list_for_each_entry(rfkill, &rfkill_list, node) {
		mutex_lock(&rfkill->mutex);
		rfkill_toggle_radio(rfkill, RFKILL_STATE_SOFT_BLOCKED, 1);
		mutex_unlock(&rfkill->mutex);
	}
	mutex_unlock(&rfkill_mutex);
}
EXPORT_SYMBOL_GPL(rfkill_epo);

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

	if (state != RFKILL_STATE_SOFT_BLOCKED &&
	    state != RFKILL_STATE_UNBLOCKED &&
	    state != RFKILL_STATE_HARD_BLOCKED)
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
	unsigned int state = simple_strtoul(buf, NULL, 0);
	int error;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

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
	bool claim = !!simple_strtoul(buf, NULL, 0);
	int error;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (rfkill->user_claim_unsupported)
		return -EOPNOTSUPP;

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
					    rfkill_states[rfkill->type],
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

static int rfkill_add_switch(struct rfkill *rfkill)
{
	mutex_lock(&rfkill_mutex);

	rfkill_toggle_radio(rfkill, rfkill_states[rfkill->type], 0);

	list_add_tail(&rfkill->node, &rfkill_list);

	mutex_unlock(&rfkill_mutex);

	return 0;
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
struct rfkill *rfkill_allocate(struct device *parent, enum rfkill_type type)
{
	struct rfkill *rfkill;
	struct device *dev;

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
int rfkill_register(struct rfkill *rfkill)
{
	static atomic_t rfkill_no = ATOMIC_INIT(0);
	struct device *dev = &rfkill->dev;
	int error;

	if (!rfkill->toggle_radio)
		return -EINVAL;
	if (rfkill->type >= RFKILL_TYPE_MAX)
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
	device_del(&rfkill->dev);
	rfkill_remove_switch(rfkill);
	rfkill_led_trigger_unregister(rfkill);
	put_device(&rfkill->dev);
}
EXPORT_SYMBOL(rfkill_unregister);

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

	for (i = 0; i < ARRAY_SIZE(rfkill_states); i++)
		rfkill_states[i] = rfkill_default_state;

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
