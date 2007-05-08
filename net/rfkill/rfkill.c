/*
 * Copyright (C) 2006 Ivo van Doorn
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

MODULE_AUTHOR("Ivo van Doorn <IvDoorn@gmail.com>");
MODULE_VERSION("1.0");
MODULE_DESCRIPTION("RF switch support");
MODULE_LICENSE("GPL");

static LIST_HEAD(rfkill_list);	/* list of registered rf switches */
static DEFINE_MUTEX(rfkill_mutex);

static enum rfkill_state rfkill_states[RFKILL_TYPE_MAX];

static int rfkill_toggle_radio(struct rfkill *rfkill,
				enum rfkill_state state)
{
	int retval;

	retval = mutex_lock_interruptible(&rfkill->mutex);
	if (retval)
		return retval;

	if (state != rfkill->state) {
		retval = rfkill->toggle_radio(rfkill->data, state);
		if (!retval)
			rfkill->state = state;
	}

	mutex_unlock(&rfkill->mutex);
	return retval;
}

/**
 * rfkill_switch_all - Toggle state of all switches of given type
 * @type: type of interfaces to be affeceted
 * @state: the new state
 *
 * This function toggles state of all switches of given type unless
 * a specific switch is claimed by userspace in which case it is
 * left alone.
 */

void rfkill_switch_all(enum rfkill_type type, enum rfkill_state state)
{
	struct rfkill *rfkill;

	mutex_lock(&rfkill_mutex);

	rfkill_states[type] = state;

	list_for_each_entry(rfkill, &rfkill_list, node) {
		if (!rfkill->user_claim)
			rfkill_toggle_radio(rfkill, state);
	}

	mutex_unlock(&rfkill_mutex);
}
EXPORT_SYMBOL(rfkill_switch_all);

static ssize_t rfkill_name_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%s\n", rfkill->name);
}

static ssize_t rfkill_type_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);
	const char *type;

	switch (rfkill->type) {
	case RFKILL_TYPE_WLAN:
		type = "wlan";
		break;
	case RFKILL_TYPE_BLUETOOTH:
		type = "bluetooth";
		break;
	case RFKILL_TYPE_IRDA:
		type = "irda";
		break;
	default:
		BUG();
	}

	return sprintf(buf, "%s\n", type);
}

static ssize_t rfkill_state_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

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

	error = rfkill_toggle_radio(rfkill,
			state ? RFKILL_STATE_ON : RFKILL_STATE_OFF);
	if (error)
		return error;

	return count;
}

static ssize_t rfkill_claim_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct rfkill *rfkill = to_rfkill(dev);

	return sprintf(buf, "%d", rfkill->user_claim);
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

	/*
	 * Take the global lock to make sure the kernel is not in
	 * the middle of rfkill_switch_all
	 */
	error = mutex_lock_interruptible(&rfkill_mutex);
	if (error)
		return error;

	if (rfkill->user_claim != claim) {
		if (!claim)
			rfkill_toggle_radio(rfkill,
					    rfkill_states[rfkill->type]);
		rfkill->user_claim = claim;
	}

	mutex_unlock(&rfkill_mutex);

	return count;
}

static struct device_attribute rfkill_dev_attrs[] = {
	__ATTR(name, S_IRUGO, rfkill_name_show, NULL),
	__ATTR(type, S_IRUGO, rfkill_type_show, NULL),
	__ATTR(state, S_IRUGO, rfkill_state_show, rfkill_state_store),
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
		if (state.event == PM_EVENT_SUSPEND) {
			mutex_lock(&rfkill->mutex);

			if (rfkill->state == RFKILL_STATE_ON)
				rfkill->toggle_radio(rfkill->data,
						     RFKILL_STATE_OFF);

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

		if (rfkill->state == RFKILL_STATE_ON)
			rfkill->toggle_radio(rfkill->data, RFKILL_STATE_ON);

		mutex_unlock(&rfkill->mutex);
	}

	dev->power.power_state = PMSG_ON;
	return 0;
}
#else
#define rfkill_suspend NULL
#define rfkill_resume NULL
#endif

static struct class rfkill_class = {
	.name		= "rfkill",
	.dev_release	= rfkill_release,
	.dev_attrs	= rfkill_dev_attrs,
	.suspend	= rfkill_suspend,
	.resume		= rfkill_resume,
};

static int rfkill_add_switch(struct rfkill *rfkill)
{
	int retval;

	retval = mutex_lock_interruptible(&rfkill_mutex);
	if (retval)
		return retval;

	retval = rfkill_toggle_radio(rfkill, rfkill_states[rfkill->type]);
	if (retval)
		goto out;

	list_add_tail(&rfkill->node, &rfkill_list);

 out:
	mutex_unlock(&rfkill_mutex);
	return retval;
}

static void rfkill_remove_switch(struct rfkill *rfkill)
{
	mutex_lock(&rfkill_mutex);
	list_del_init(&rfkill->node);
	rfkill_toggle_radio(rfkill, RFKILL_STATE_OFF);
	mutex_unlock(&rfkill_mutex);
}

/**
 * rfkill_allocate - allocate memory for rfkill structure.
 * @parent: device that has rf switch on it
 * @type: type of the switch (wlan, bluetooth, irda)
 *
 * This function should be called by the network driver when it needs
 * rfkill structure. Once the structure is allocated the driver shoud
 * finish its initialization by setting name, private data, enable_radio
 * and disable_radio methods and then register it with rfkill_register().
 * NOTE: If registration fails the structure shoudl be freed by calling
 * rfkill_free() otherwise rfkill_unregister() should be used.
 */
struct rfkill *rfkill_allocate(struct device *parent, enum rfkill_type type)
{
	struct rfkill *rfkill;
	struct device *dev;

	rfkill = kzalloc(sizeof(struct rfkill), GFP_KERNEL);
	if (rfkill)
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
 * Decrements reference count of rfkill structure so it is destoryed.
 * Note that rfkill_free() should _not_ be called after rfkill_unregister().
 */
void rfkill_free(struct rfkill *rfkill)
{
	if (rfkill)
		put_device(&rfkill->dev);
}
EXPORT_SYMBOL(rfkill_free);

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

	error = rfkill_add_switch(rfkill);
	if (error)
		return error;

	snprintf(dev->bus_id, sizeof(dev->bus_id),
		 "rfkill%ld", (long)atomic_inc_return(&rfkill_no) - 1);

	error = device_add(dev);
	if (error) {
		rfkill_remove_switch(rfkill);
		return error;
	}

	return 0;
}
EXPORT_SYMBOL(rfkill_register);

/**
 * rfkill_unregister - Uegister a rfkill structure.
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

	for (i = 0; i < ARRAY_SIZE(rfkill_states); i++)
		rfkill_states[i] = RFKILL_STATE_ON;

	error = class_register(&rfkill_class);
	if (error) {
		printk(KERN_ERR "rfkill: unable to register rfkill class\n");
		return error;
	}

	return 0;
}

static void __exit rfkill_exit(void)
{
	class_unregister(&rfkill_class);
}

module_init(rfkill_init);
module_exit(rfkill_exit);
