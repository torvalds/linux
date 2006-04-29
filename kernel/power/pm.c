/*
 *  pm.c - Power management interface
 *
 *  Copyright (C) 2000 Andrew Henroid
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/pm_legacy.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>

int pm_active;

/*
 *	Locking notes:
 *		pm_devs_lock can be a semaphore providing pm ops are not called
 *	from an interrupt handler (already a bad idea so no change here). Each
 *	change must be protected so that an unlink of an entry doesn't clash
 *	with a pm send - which is permitted to sleep in the current architecture
 *
 *	Module unloads clashing with pm events now work out safely, the module 
 *	unload path will block until the event has been sent. It may well block
 *	until a resume but that will be fine.
 */
 
static DEFINE_MUTEX(pm_devs_lock);
static LIST_HEAD(pm_devs);

/**
 *	pm_register - register a device with power management
 *	@type: device type 
 *	@id: device ID
 *	@callback: callback function
 *
 *	Add a device to the list of devices that wish to be notified about
 *	power management events. A &pm_dev structure is returned on success,
 *	on failure the return is %NULL.
 *
 *      The callback function will be called in process context and
 *      it may sleep.
 */
 
struct pm_dev *pm_register(pm_dev_t type,
			   unsigned long id,
			   pm_callback callback)
{
	struct pm_dev *dev = kzalloc(sizeof(struct pm_dev), GFP_KERNEL);
	if (dev) {
		dev->type = type;
		dev->id = id;
		dev->callback = callback;

		mutex_lock(&pm_devs_lock);
		list_add(&dev->entry, &pm_devs);
		mutex_unlock(&pm_devs_lock);
	}
	return dev;
}

static void __pm_unregister(struct pm_dev *dev)
{
	if (dev) {
		list_del(&dev->entry);
		kfree(dev);
	}
}

/**
 *	pm_unregister_all - unregister all devices with matching callback
 *	@callback: callback function pointer
 *
 *	Unregister every device that would call the callback passed. This
 *	is primarily meant as a helper function for loadable modules. It
 *	enables a module to give up all its managed devices without keeping
 *	its own private list.
 */
 
void pm_unregister_all(pm_callback callback)
{
	struct list_head *entry;

	if (!callback)
		return;

	mutex_lock(&pm_devs_lock);
	entry = pm_devs.next;
	while (entry != &pm_devs) {
		struct pm_dev *dev = list_entry(entry, struct pm_dev, entry);
		entry = entry->next;
		if (dev->callback == callback)
			__pm_unregister(dev);
	}
	mutex_unlock(&pm_devs_lock);
}

/**
 *	pm_send - send request to a single device
 *	@dev: device to send to
 *	@rqst: power management request
 *	@data: data for the callback
 *
 *	Issue a power management request to a given device. The 
 *	%PM_SUSPEND and %PM_RESUME events are handled specially. The
 *	data field must hold the intended next state. No call is made
 *	if the state matches.
 *
 *	BUGS: what stops two power management requests occurring in parallel
 *	and conflicting.
 *
 *	WARNING: Calling pm_send directly is not generally recommended, in
 *	particular there is no locking against the pm_dev going away. The
 *	caller must maintain all needed locking or have 'inside knowledge'
 *	on the safety. Also remember that this function is not locked against
 *	pm_unregister. This means that you must handle SMP races on callback
 *	execution and unload yourself.
 */
 
static int pm_send(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	int status = 0;
	unsigned long prev_state, next_state;

	if (in_interrupt())
		BUG();

	switch (rqst) {
	case PM_SUSPEND:
	case PM_RESUME:
		prev_state = dev->state;
		next_state = (unsigned long) data;
		if (prev_state != next_state) {
			if (dev->callback)
				status = (*dev->callback)(dev, rqst, data);
			if (!status) {
				dev->state = next_state;
				dev->prev_state = prev_state;
			}
		}
		else {
			dev->prev_state = prev_state;
		}
		break;
	default:
		if (dev->callback)
			status = (*dev->callback)(dev, rqst, data);
		break;
	}
	return status;
}

/*
 * Undo incomplete request
 */
static void pm_undo_all(struct pm_dev *last)
{
	struct list_head *entry = last->entry.prev;
	while (entry != &pm_devs) {
		struct pm_dev *dev = list_entry(entry, struct pm_dev, entry);
		if (dev->state != dev->prev_state) {
			/* previous state was zero (running) resume or
			 * previous state was non-zero (suspended) suspend
			 */
			pm_request_t undo = (dev->prev_state
					     ? PM_SUSPEND:PM_RESUME);
			pm_send(dev, undo, (void*) dev->prev_state);
		}
		entry = entry->prev;
	}
}

/**
 *	pm_send_all - send request to all managed devices
 *	@rqst: power management request
 *	@data: data for the callback
 *
 *	Issue a power management request to a all devices. The 
 *	%PM_SUSPEND events are handled specially. Any device is 
 *	permitted to fail a suspend by returning a non zero (error)
 *	value from its callback function. If any device vetoes a 
 *	suspend request then all other devices that have suspended 
 *	during the processing of this request are restored to their
 *	previous state.
 *
 *	WARNING:  This function takes the pm_devs_lock. The lock is not dropped until
 *	the callbacks have completed. This prevents races against pm locking
 *	functions, races against module unload pm_unregister code. It does
 *	mean however that you must not issue pm_ functions within the callback
 *	or you will deadlock and users will hate you.
 *
 *	Zero is returned on success. If a suspend fails then the status
 *	from the device that vetoes the suspend is returned.
 *
 *	BUGS: what stops two power management requests occurring in parallel
 *	and conflicting.
 */
 
int pm_send_all(pm_request_t rqst, void *data)
{
	struct list_head *entry;
	
	mutex_lock(&pm_devs_lock);
	entry = pm_devs.next;
	while (entry != &pm_devs) {
		struct pm_dev *dev = list_entry(entry, struct pm_dev, entry);
		if (dev->callback) {
			int status = pm_send(dev, rqst, data);
			if (status) {
				/* return devices to previous state on
				 * failed suspend request
				 */
				if (rqst == PM_SUSPEND)
					pm_undo_all(dev);
				mutex_unlock(&pm_devs_lock);
				return status;
			}
		}
		entry = entry->next;
	}
	mutex_unlock(&pm_devs_lock);
	return 0;
}

EXPORT_SYMBOL(pm_register);
EXPORT_SYMBOL(pm_unregister_all);
EXPORT_SYMBOL(pm_send_all);
EXPORT_SYMBOL(pm_active);


