/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 */

/* This file implements VMCI Event code. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "vmci.h"
#include "vmci_driver.h"
#include "vmci_event.h"
#include "vmci_kernel_api.h"
#include "vmci_kernel_defs.h"
#include "vmci_kernel_if.h"

#define LGPFX		"vmci_event: "
#define EVENT_MAGIC	0xEABE0000

struct vmci_subscription {
	vmci_id		id;
	int		ref_count;
	bool		run_delayed;
	vmci_event	destroy_event;
	vmci_event_type	event;
	vmci_event_cb	callback;
	void		*callback_data;
	vmci_list_item(vmci_subscription) subscriber_list_item;
};

static struct	vmci_subscription *vmci_event_find(vmci_id sub_id);
static int	vmci_event_deliver(struct vmci_event_msg *event_msg);
static int	vmci_event_register_subscription(struct vmci_subscription *sub,
		    vmci_event_type event, uint32_t flags,
		    vmci_event_cb callback, void *callback_data);
static struct	vmci_subscription *vmci_event_unregister_subscription(
		    vmci_id sub_id);

static vmci_list(vmci_subscription) subscriber_array[VMCI_EVENT_MAX];
static vmci_lock subscriber_lock;

struct vmci_delayed_event_info {
	struct vmci_subscription *sub;
	uint8_t event_payload[sizeof(struct vmci_event_data_max)];
};

struct vmci_event_ref {
	struct vmci_subscription	*sub;
	vmci_list_item(vmci_event_ref)	list_item;
};

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_init --
 *
 *     General init code.
 *
 * Results:
 *     VMCI_SUCCESS on success, appropriate error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_event_init(void)
{
	int i;

	for (i = 0; i < VMCI_EVENT_MAX; i++)
		vmci_list_init(&subscriber_array[i]);

	return (vmci_init_lock(&subscriber_lock, "VMCI Event subscriber lock"));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_exit --
 *
 *     General exit code.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_event_exit(void)
{
	struct vmci_subscription *iter, *iter_2;
	vmci_event_type e;

	/* We free all memory at exit. */
	for (e = 0; e < VMCI_EVENT_MAX; e++) {
		vmci_list_scan_safe(iter, &subscriber_array[e],
		    subscriber_list_item, iter_2) {

			/*
			 * We should never get here because all events should
			 * have been unregistered before we try to unload the
			 * driver module. Also, delayed callbacks could still
			 * be firing so this cleanup would not be safe. Still
			 * it is better to free the memory than not ... so we
			 * leave this code in just in case....
			 */
			ASSERT(false);

			vmci_free_kernel_mem(iter, sizeof(*iter));
		}
	}
	vmci_cleanup_lock(&subscriber_lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_sync --
 *
 *     Use this as a synchronization point when setting globals, for example,
 *     during device shutdown.
 *
 * Results:
 *     true.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_event_sync(void)
{

	vmci_grab_lock_bh(&subscriber_lock);
	vmci_release_lock_bh(&subscriber_lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_check_host_capabilities --
 *
 *     Verify that the host supports the hypercalls we need. If it does not,
 *     try to find fallback hypercalls and use those instead.
 *
 * Results:
 *     true if required hypercalls (or fallback hypercalls) are
 *     supported by the host, false otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

bool
vmci_event_check_host_capabilities(void)
{

	/* vmci_event does not require any hypercalls. */
	return (true);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_get --
 *
 *     Gets a reference to the given struct vmci_subscription.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_event_get(struct vmci_subscription *entry)
{

	ASSERT(entry);

	entry->ref_count++;
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_release --
 *
 *     Releases the given struct vmci_subscription.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Fires the destroy event if the reference count has gone to zero.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_event_release(struct vmci_subscription *entry)
{

	ASSERT(entry);
	ASSERT(entry->ref_count > 0);

	entry->ref_count--;
	if (entry->ref_count == 0)
		vmci_signal_event(&entry->destroy_event);
}

 /*
 *------------------------------------------------------------------------------
 *
 * event_release_cb --
 *
 *     Callback to release the event entry reference. It is called by the
 *     vmci_wait_on_event function before it blocks.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
event_release_cb(void *client_data)
{
	struct vmci_subscription *sub = (struct vmci_subscription *)client_data;

	ASSERT(sub);

	vmci_grab_lock_bh(&subscriber_lock);
	vmci_event_release(sub);
	vmci_release_lock_bh(&subscriber_lock);

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_find --
 *
 *     Find entry. Assumes lock is held.
 *
 * Results:
 *     Entry if found, NULL if not.
 *
 * Side effects:
 *     Increments the struct vmci_subscription refcount if an entry is found.
 *
 *------------------------------------------------------------------------------
 */

static struct vmci_subscription *
vmci_event_find(vmci_id sub_id)
{
	struct vmci_subscription *iter;
	vmci_event_type e;

	for (e = 0; e < VMCI_EVENT_MAX; e++) {
		vmci_list_scan(iter, &subscriber_array[e],
		    subscriber_list_item) {
			if (iter->id == sub_id) {
				vmci_event_get(iter);
				return (iter);
			}
		}
	}
	return (NULL);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_delayed_dispatch_cb --
 *
 *     Calls the specified callback in a delayed context.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_event_delayed_dispatch_cb(void *data)
{
	struct vmci_delayed_event_info *event_info;
	struct vmci_subscription *sub;
	struct vmci_event_data *ed;

	event_info = (struct vmci_delayed_event_info *)data;

	ASSERT(event_info);
	ASSERT(event_info->sub);

	sub = event_info->sub;
	ed = (struct vmci_event_data *)event_info->event_payload;

	sub->callback(sub->id, ed, sub->callback_data);

	vmci_grab_lock_bh(&subscriber_lock);
	vmci_event_release(sub);
	vmci_release_lock_bh(&subscriber_lock);

	vmci_free_kernel_mem(event_info, sizeof(*event_info));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_deliver --
 *
 *     Actually delivers the events to the subscribers.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     The callback function for each subscriber is invoked.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_event_deliver(struct vmci_event_msg *event_msg)
{
	struct vmci_subscription *iter;
	int err = VMCI_SUCCESS;

	vmci_list(vmci_event_ref) no_delay_list;
	vmci_list_init(&no_delay_list);

	ASSERT(event_msg);

	vmci_grab_lock_bh(&subscriber_lock);
	vmci_list_scan(iter, &subscriber_array[event_msg->event_data.event],
	    subscriber_list_item) {
		if (iter->run_delayed) {
			struct vmci_delayed_event_info *event_info;
			if ((event_info =
			    vmci_alloc_kernel_mem(sizeof(*event_info),
			    VMCI_MEMORY_ATOMIC)) == NULL) {
				err = VMCI_ERROR_NO_MEM;
				goto out;
			}

			vmci_event_get(iter);

			memset(event_info, 0, sizeof(*event_info));
			memcpy(event_info->event_payload,
			    VMCI_DG_PAYLOAD(event_msg),
			    (size_t)event_msg->hdr.payload_size);
			event_info->sub = iter;
			err =
			    vmci_schedule_delayed_work(
			    vmci_event_delayed_dispatch_cb, event_info);
			if (err != VMCI_SUCCESS) {
				vmci_event_release(iter);
				vmci_free_kernel_mem(
				    event_info, sizeof(*event_info));
				goto out;
			}

		} else {
			struct vmci_event_ref *event_ref;

			/*
			 * We construct a local list of subscribers and release
			 * subscriber_lock before invoking the callbacks. This
			 * is similar to delayed callbacks, but callbacks are
			 * invoked right away here.
			 */
			if ((event_ref = vmci_alloc_kernel_mem(
			    sizeof(*event_ref), VMCI_MEMORY_ATOMIC)) == NULL) {
				err = VMCI_ERROR_NO_MEM;
				goto out;
			}

			vmci_event_get(iter);
			event_ref->sub = iter;
			vmci_list_insert(&no_delay_list, event_ref, list_item);
		}
	}

out:
	vmci_release_lock_bh(&subscriber_lock);

	if (!vmci_list_empty(&no_delay_list)) {
		struct vmci_event_data *ed;
		struct vmci_event_ref *iter;
		struct vmci_event_ref *iter_2;

		vmci_list_scan_safe(iter, &no_delay_list, list_item, iter_2) {
			struct vmci_subscription *cur;
			uint8_t event_payload[sizeof(
			    struct vmci_event_data_max)];

			cur = iter->sub;

			/*
			 * We set event data before each callback to ensure
			 * isolation.
			 */
			memset(event_payload, 0, sizeof(event_payload));
			memcpy(event_payload, VMCI_DG_PAYLOAD(event_msg),
			    (size_t)event_msg->hdr.payload_size);
			ed = (struct vmci_event_data *)event_payload;
			cur->callback(cur->id, ed, cur->callback_data);

			vmci_grab_lock_bh(&subscriber_lock);
			vmci_event_release(cur);
			vmci_release_lock_bh(&subscriber_lock);
			vmci_free_kernel_mem(iter, sizeof(*iter));
		}
	}

	return (err);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_dispatch --
 *
 *     Dispatcher for the VMCI_EVENT_RECEIVE datagrams. Calls all
 *     subscribers for given event.
 *
 * Results:
 *     VMCI_SUCCESS on success, error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_event_dispatch(struct vmci_datagram *msg)
{
	struct vmci_event_msg *event_msg = (struct vmci_event_msg *)msg;

	ASSERT(msg &&
	    msg->src.context == VMCI_HYPERVISOR_CONTEXT_ID &&
	    msg->dst.resource == VMCI_EVENT_HANDLER);

	if (msg->payload_size < sizeof(vmci_event_type) ||
	    msg->payload_size > sizeof(struct vmci_event_data_max))
		return (VMCI_ERROR_INVALID_ARGS);

	if (!VMCI_EVENT_VALID(event_msg->event_data.event))
		return (VMCI_ERROR_EVENT_UNKNOWN);

	vmci_event_deliver(event_msg);

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_register_subscription --
 *
 *     Initialize and add subscription to subscriber list.
 *
 * Results:
 *     VMCI_SUCCESS on success, error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_event_register_subscription(struct vmci_subscription *sub,
    vmci_event_type event, uint32_t flags, vmci_event_cb callback,
    void *callback_data)
{
#define VMCI_EVENT_MAX_ATTEMPTS	10
	static vmci_id subscription_id = 0;
	int result;
	uint32_t attempts = 0;
	bool success;

	ASSERT(sub);

	if (!VMCI_EVENT_VALID(event) || callback == NULL) {
		VMCI_LOG_DEBUG(LGPFX"Failed to subscribe to event"
		    " (type=%d) (callback=%p) (data=%p).\n",
		    event, callback, callback_data);
		return (VMCI_ERROR_INVALID_ARGS);
	}

	if (!vmci_can_schedule_delayed_work()) {
		/*
		 * If the platform doesn't support delayed work callbacks then
		 * don't allow registration for them.
		 */
		if (flags & VMCI_FLAG_EVENT_DELAYED_CB)
			return (VMCI_ERROR_INVALID_ARGS);
		sub->run_delayed = false;
	} else {
		/*
		 * The platform supports delayed work callbacks. Honor the
		 * requested flags
		 */
		sub->run_delayed = (flags & VMCI_FLAG_EVENT_DELAYED_CB) ?
		    true : false;
	}

	sub->ref_count = 1;
	sub->event = event;
	sub->callback = callback;
	sub->callback_data = callback_data;

	vmci_grab_lock_bh(&subscriber_lock);

	for (success = false, attempts = 0;
	    success == false && attempts < VMCI_EVENT_MAX_ATTEMPTS;
	    attempts++) {
		struct vmci_subscription *existing_sub = NULL;

		/*
		 * We try to get an id a couple of time before claiming we are
		 * out of resources.
		 */
		sub->id = ++subscription_id;

		/* Test for duplicate id. */
		existing_sub = vmci_event_find(sub->id);
		if (existing_sub == NULL) {
			/* We succeeded if we didn't find a duplicate. */
			success = true;
		} else
			vmci_event_release(existing_sub);
	}

	if (success) {
		vmci_create_event(&sub->destroy_event);
		vmci_list_insert(&subscriber_array[event], sub,
		    subscriber_list_item);
		result = VMCI_SUCCESS;
	} else
		result = VMCI_ERROR_NO_RESOURCES;

	vmci_release_lock_bh(&subscriber_lock);
	return (result);
#undef VMCI_EVENT_MAX_ATTEMPTS
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_unregister_subscription --
 *
 *     Remove subscription from subscriber list.
 *
 * Results:
 *     struct vmci_subscription when found, NULL otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static struct vmci_subscription *
vmci_event_unregister_subscription(vmci_id sub_id)
{
	struct vmci_subscription *s;

	vmci_grab_lock_bh(&subscriber_lock);
	s = vmci_event_find(sub_id);
	if (s != NULL) {
		vmci_event_release(s);
		vmci_list_remove(s, subscriber_list_item);
	}
	vmci_release_lock_bh(&subscriber_lock);

	if (s != NULL) {
		vmci_wait_on_event(&s->destroy_event, event_release_cb, s);
		vmci_destroy_event(&s->destroy_event);
	}

	return (s);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_subscribe --
 *
 *     Subscribe to given event. The callback specified can be fired in
 *     different contexts depending on what flag is specified while registering.
 *     If flags contains VMCI_FLAG_EVENT_NONE then the callback is fired with
 *     the subscriber lock held (and BH context on the guest). If flags contain
 *     VMCI_FLAG_EVENT_DELAYED_CB then the callback is fired with no locks held
 *     in thread context. This is useful because other vmci_event functions can
 *     be called, but it also increases the chances that an event will be
 *     dropped.
 *
 * Results:
 *     VMCI_SUCCESS on success, error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_event_subscribe(vmci_event_type event, vmci_event_cb callback,
    void *callback_data, vmci_id *subscription_id)
{
	int retval;
	uint32_t flags = VMCI_FLAG_EVENT_NONE;
	struct vmci_subscription *s = NULL;

	if (subscription_id == NULL) {
		VMCI_LOG_DEBUG(LGPFX"Invalid subscription (NULL).\n");
		return (VMCI_ERROR_INVALID_ARGS);
	}

	s = vmci_alloc_kernel_mem(sizeof(*s), VMCI_MEMORY_NORMAL);
	if (s == NULL)
		return (VMCI_ERROR_NO_MEM);

	retval = vmci_event_register_subscription(s, event, flags,
	    callback, callback_data);
	if (retval < VMCI_SUCCESS) {
		vmci_free_kernel_mem(s, sizeof(*s));
		return (retval);
	}

	*subscription_id = s->id;
	return (retval);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_event_unsubscribe --
 *
 *     Unsubscribe to given event. Removes it from list and frees it.
 *     Will return callback_data if requested by caller.
 *
 * Results:
 *     VMCI_SUCCESS on success, error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_event_unsubscribe(vmci_id sub_id)
{
	struct vmci_subscription *s;

	/*
	 * Return subscription. At this point we know noone else is accessing
	 * the subscription so we can free it.
	 */
	s = vmci_event_unregister_subscription(sub_id);
	if (s == NULL)
		return (VMCI_ERROR_NOT_FOUND);
	vmci_free_kernel_mem(s, sizeof(*s));

	return (VMCI_SUCCESS);
}
