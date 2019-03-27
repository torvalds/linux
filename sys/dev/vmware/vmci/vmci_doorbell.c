/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 */

/* This file implements the VMCI doorbell API. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include "vmci_doorbell.h"
#include "vmci_driver.h"
#include "vmci_kernel_api.h"
#include "vmci_kernel_defs.h"
#include "vmci_resource.h"
#include "vmci_utils.h"

#define LGPFX				"vmci_doorbell: "

#define VMCI_DOORBELL_INDEX_TABLE_SIZE	64
#define VMCI_DOORBELL_HASH(_idx)					\
	vmci_hash_id((_idx), VMCI_DOORBELL_INDEX_TABLE_SIZE)

/* Describes a doorbell notification handle allocated by the host. */
struct vmci_doorbell_entry {
	struct vmci_resource			resource;
	uint32_t				idx;
	vmci_list_item(vmci_doorbell_entry)	idx_list_item;
	vmci_privilege_flags			priv_flags;
	bool					is_doorbell;
	bool					run_delayed;
	vmci_callback				notify_cb;
	void					*client_data;
	vmci_event				destroy_event;
	volatile int				active;
};

struct vmci_doorbell_index_table {
	vmci_lock			lock;
	vmci_list(vmci_doorbell_entry)	entries[VMCI_DOORBELL_INDEX_TABLE_SIZE];
};

/* The VMCI index table keeps track of currently registered doorbells. */
static struct vmci_doorbell_index_table vmci_doorbell_it;

/*
 * The max_notify_idx is one larger than the currently known bitmap index in
 * use, and is used to determine how much of the bitmap needs to be scanned.
 */
static uint32_t	max_notify_idx;

/*
 * The notify_idx_count is used for determining whether there are free entries
 * within the bitmap (if notify_idx_count + 1 < max_notify_idx).
 */
static uint32_t notify_idx_count;

/*
 * The last_notify_idx_reserved is used to track the last index handed out - in
 * the case where multiple handles share a notification index, we hand out
 * indexes round robin based on last_notify_idx_reserved.
 */
static uint32_t last_notify_idx_reserved;

/* This is a one entry cache used to by the index allocation. */
static uint32_t last_notify_idx_released = PAGE_SIZE;

static void	vmci_doorbell_free_cb(void *client_data);
static int	vmci_doorbell_release_cb(void *client_data);
static void	vmci_doorbell_delayed_dispatch_cb(void *data);

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_init --
 *
 *     General init code.
 *
 * Result:
 *     VMCI_SUCCESS on success, lock allocation error otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_doorbell_init(void)
{
	uint32_t bucket;

	for (bucket = 0; bucket < ARRAYSIZE(vmci_doorbell_it.entries);
	    ++bucket)
		vmci_list_init(&vmci_doorbell_it.entries[bucket]);

	return (vmci_init_lock(&vmci_doorbell_it.lock,
	    "VMCI Doorbell index table lock"));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_exit --
 *
 *     General exit code.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_doorbell_exit(void)
{

	vmci_cleanup_lock(&vmci_doorbell_it.lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_free_cb --
 *
 *     Callback to free doorbell entry structure when resource is no longer used,
 *     i.e. the reference count reached 0.  The entry is freed in
 *     vmci_doorbell_destroy(), which is waiting on the signal that gets fired
 *     here.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     Signals VMCI event.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_doorbell_free_cb(void *client_data)
{
	struct vmci_doorbell_entry *entry;

	entry = (struct vmci_doorbell_entry *)client_data;
	ASSERT(entry);
	vmci_signal_event(&entry->destroy_event);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_release_cb --
 *
 *     Callback to release the resource reference. It is called by the
 *     vmci_wait_on_event function before it blocks.
 *
 * Result:
 *     Always 0.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_doorbell_release_cb(void *client_data)
{
	struct vmci_doorbell_entry *entry;

	entry  = (struct vmci_doorbell_entry *)client_data;
	ASSERT(entry);
	vmci_resource_release(&entry->resource);
	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_get_priv_flags --
 *
 *     Utility function that retrieves the privilege flags associated with a
 *     given doorbell handle. For guest endpoints, the privileges are determined
 *     by the context ID, but for host endpoints privileges are associated with
 *     the complete handle. Hypervisor endpoints are not yet supported.
 *
 * Result:
 *     VMCI_SUCCESS on success,
 *     VMCI_ERROR_NOT_FOUND if handle isn't found,
 *     VMCI_ERROR_INVALID_ARGS if handle is invalid.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_doorbell_get_priv_flags(struct vmci_handle handle,
    vmci_privilege_flags *priv_flags)
{

	if (priv_flags == NULL || handle.context == VMCI_INVALID_ID)
		return (VMCI_ERROR_INVALID_ARGS);

	if (handle.context == VMCI_HOST_CONTEXT_ID) {
		struct vmci_doorbell_entry *entry;
		struct vmci_resource *resource;

		resource = vmci_resource_get(handle,
		    VMCI_RESOURCE_TYPE_DOORBELL);
		if (resource == NULL)
			return (VMCI_ERROR_NOT_FOUND);
		entry = RESOURCE_CONTAINER(
		    resource, struct vmci_doorbell_entry, resource);
		*priv_flags = entry->priv_flags;
		vmci_resource_release(resource);
	} else if (handle.context == VMCI_HYPERVISOR_CONTEXT_ID) {
		/* Hypervisor endpoints for notifications are not supported. */
		return (VMCI_ERROR_INVALID_ARGS);
	} else
		*priv_flags = VMCI_NO_PRIVILEGE_FLAGS;

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_index_table_find --
 *
 *     Find doorbell entry by bitmap index.
 *
 * Results:
 *     Entry if found, NULL if not.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static struct vmci_doorbell_entry *
vmci_doorbell_index_table_find(uint32_t idx)
{
	struct vmci_doorbell_entry *iter;
	uint32_t bucket;

	bucket = VMCI_DOORBELL_HASH(idx);

	vmci_list_scan(iter, &vmci_doorbell_it.entries[bucket], idx_list_item) {
		if (idx == iter->idx)
			return (iter);
	}

	return (NULL);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_index_table_add --
 *
 *     Add the given entry to the index table. This will hold() the entry's
 *     resource so that the entry is not deleted before it is removed from the
 *     table.
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
vmci_doorbell_index_table_add(struct vmci_doorbell_entry *entry)
{
	uint32_t bucket;
	uint32_t new_notify_idx;

	ASSERT(entry);

	vmci_resource_hold(&entry->resource);

	vmci_grab_lock_bh(&vmci_doorbell_it.lock);

	/*
	 * Below we try to allocate an index in the notification bitmap with
	 * "not too much" sharing between resources. If we use less that the
	 * full bitmap, we either add to the end if there are no unused flags
	 * within the currently used area, or we search for unused ones. If we
	 * use the full bitmap, we allocate the index round robin.
	 */

	if (max_notify_idx < PAGE_SIZE || notify_idx_count < PAGE_SIZE) {
		if (last_notify_idx_released < max_notify_idx &&
		    !vmci_doorbell_index_table_find(last_notify_idx_released)) {
			new_notify_idx = last_notify_idx_released;
			last_notify_idx_released = PAGE_SIZE;
		} else {
			bool reused = false;
			new_notify_idx = last_notify_idx_reserved;
			if (notify_idx_count + 1 < max_notify_idx) {
				do {
					if (!vmci_doorbell_index_table_find(
					    new_notify_idx)) {
						reused = true;
						break;
					}
					new_notify_idx = (new_notify_idx + 1) %
					    max_notify_idx;
				} while (new_notify_idx !=
				    last_notify_idx_released);
			}
			if (!reused) {
				new_notify_idx = max_notify_idx;
				max_notify_idx++;
			}
		}
	} else {
		new_notify_idx = (last_notify_idx_reserved + 1) % PAGE_SIZE;
	}
	last_notify_idx_reserved = new_notify_idx;
	notify_idx_count++;

	entry->idx = new_notify_idx;
	bucket = VMCI_DOORBELL_HASH(entry->idx);
	vmci_list_insert(&vmci_doorbell_it.entries[bucket], entry,
	    idx_list_item);

	vmci_release_lock_bh(&vmci_doorbell_it.lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_index_table_remove --
 *
 *     Remove the given entry from the index table. This will release() the
 *     entry's resource.
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
vmci_doorbell_index_table_remove(struct vmci_doorbell_entry *entry)
{
	ASSERT(entry);

	vmci_grab_lock_bh(&vmci_doorbell_it.lock);

	vmci_list_remove(entry, idx_list_item);

	notify_idx_count--;
	if (entry->idx == max_notify_idx - 1) {
		/*
		 * If we delete an entry with the maximum known notification
		 * index, we take the opportunity to prune the current max. As
		 * there might be other unused indices immediately below, we
		 * lower the maximum until we hit an index in use
		 */

		while (max_notify_idx > 0 &&
		    !vmci_doorbell_index_table_find(max_notify_idx - 1))
			max_notify_idx--;
	}
	last_notify_idx_released = entry->idx;

	vmci_release_lock_bh(&vmci_doorbell_it.lock);

	vmci_resource_release(&entry->resource);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_link --
 *
 *     Creates a link between the given doorbell handle and the given index in
 *     the bitmap in the device backend.
 *
 * Results:
 *     VMCI_SUCCESS if success, appropriate error code otherwise.
 *
 * Side effects:
 *     Notification state is created in hypervisor.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_doorbell_link(struct vmci_handle handle, bool is_doorbell,
    uint32_t notify_idx)
{
	struct vmci_doorbell_link_msg link_msg;
	vmci_id resource_id;

	ASSERT(!VMCI_HANDLE_INVALID(handle));

	if (is_doorbell)
		resource_id = VMCI_DOORBELL_LINK;
	else {
		ASSERT(false);
		return (VMCI_ERROR_UNAVAILABLE);
	}

	link_msg.hdr.dst = VMCI_MAKE_HANDLE(VMCI_HYPERVISOR_CONTEXT_ID,
	    resource_id);
	link_msg.hdr.src = VMCI_ANON_SRC_HANDLE;
	link_msg.hdr.payload_size = sizeof(link_msg) - VMCI_DG_HEADERSIZE;
	link_msg.handle = handle;
	link_msg.notify_idx = notify_idx;

	return (vmci_send_datagram((struct vmci_datagram *)&link_msg));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_unlink --
 *
 *     Unlinks the given doorbell handle from an index in the bitmap in the
 *     device backend.
 *
 * Results:
 *     VMCI_SUCCESS if success, appropriate error code otherwise.
 *
 * Side effects:
 *     Notification state is destroyed in hypervisor.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_doorbell_unlink(struct vmci_handle handle, bool is_doorbell)
{
	struct vmci_doorbell_unlink_msg unlink_msg;
	vmci_id resource_id;

	ASSERT(!VMCI_HANDLE_INVALID(handle));

	if (is_doorbell)
		resource_id = VMCI_DOORBELL_UNLINK;
	else {
		ASSERT(false);
		return (VMCI_ERROR_UNAVAILABLE);
	}

	unlink_msg.hdr.dst = VMCI_MAKE_HANDLE(VMCI_HYPERVISOR_CONTEXT_ID,
	    resource_id);
	unlink_msg.hdr.src = VMCI_ANON_SRC_HANDLE;
	unlink_msg.hdr.payload_size = sizeof(unlink_msg) - VMCI_DG_HEADERSIZE;
	unlink_msg.handle = handle;

	return (vmci_send_datagram((struct vmci_datagram *)&unlink_msg));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_create --
 *
 *     Creates a doorbell with the given callback. If the handle is
 *     VMCI_INVALID_HANDLE, a free handle will be assigned, if possible. The
 *     callback can be run immediately (potentially with locks held - the
 *     default) or delayed (in a kernel thread) by specifying the flag
 *     VMCI_FLAG_DELAYED_CB. If delayed execution is selected, a given callback
 *     may not be run if the kernel is unable to allocate memory for the delayed
 *     execution (highly unlikely).
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
vmci_doorbell_create(struct vmci_handle *handle, uint32_t flags,
    vmci_privilege_flags priv_flags, vmci_callback notify_cb, void *client_data)
{
	struct vmci_doorbell_entry *entry;
	struct vmci_handle new_handle;
	int result;

	if (!handle || !notify_cb || flags & ~VMCI_FLAG_DELAYED_CB ||
	    priv_flags & ~VMCI_PRIVILEGE_ALL_FLAGS)
		return (VMCI_ERROR_INVALID_ARGS);

	entry = vmci_alloc_kernel_mem(sizeof(*entry), VMCI_MEMORY_NORMAL);
	if (entry == NULL) {
		VMCI_LOG_WARNING(LGPFX"Failed allocating memory for datagram "
		    "entry.\n");
		return (VMCI_ERROR_NO_MEM);
	}

	if (!vmci_can_schedule_delayed_work() &&
	    (flags & VMCI_FLAG_DELAYED_CB)) {
		result = VMCI_ERROR_INVALID_ARGS;
		goto free_mem;
	}

	if (VMCI_HANDLE_INVALID(*handle)) {
		vmci_id context_id;

		context_id = vmci_get_context_id();
		vmci_id resource_id = vmci_resource_get_id(context_id);
		if (resource_id == VMCI_INVALID_ID) {
			result = VMCI_ERROR_NO_HANDLE;
			goto free_mem;
		}
		new_handle = VMCI_MAKE_HANDLE(context_id, resource_id);
	} else {
		if (VMCI_INVALID_ID == handle->resource) {
			VMCI_LOG_DEBUG(LGPFX"Invalid argument "
			    "(handle=0x%x:0x%x).\n", handle->context,
			    handle->resource);
			result = VMCI_ERROR_INVALID_ARGS;
			goto free_mem;
		}
		new_handle = *handle;
	}

	entry->idx = 0;
	entry->priv_flags = priv_flags;
	entry->is_doorbell = true;
	entry->run_delayed = (flags & VMCI_FLAG_DELAYED_CB) ? true : false;
	entry->notify_cb = notify_cb;
	entry->client_data = client_data;
	atomic_store_int(&entry->active, 0);
	vmci_create_event(&entry->destroy_event);

	result = vmci_resource_add(&entry->resource,
	    VMCI_RESOURCE_TYPE_DOORBELL, new_handle, vmci_doorbell_free_cb,
	    entry);
	if (result != VMCI_SUCCESS) {
		VMCI_LOG_WARNING(LGPFX"Failed to add new resource "
		    "(handle=0x%x:0x%x).\n", new_handle.context,
		    new_handle.resource);
		if (result == VMCI_ERROR_DUPLICATE_ENTRY)
			result = VMCI_ERROR_ALREADY_EXISTS;

		goto destroy;
	}

	vmci_doorbell_index_table_add(entry);
	result = vmci_doorbell_link(new_handle, entry->is_doorbell, entry->idx);
	if (VMCI_SUCCESS != result)
		goto destroy_resource;
	atomic_store_int(&entry->active, 1);

	if (VMCI_HANDLE_INVALID(*handle))
		*handle = new_handle;

	return (result);

destroy_resource:
	vmci_doorbell_index_table_remove(entry);
	vmci_resource_remove(new_handle, VMCI_RESOURCE_TYPE_DOORBELL);
destroy:
	vmci_destroy_event(&entry->destroy_event);
free_mem:
	vmci_free_kernel_mem(entry, sizeof(*entry));
	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_destroy --
 *
 *     Destroys a doorbell previously created with vmci_doorbell_create. This
 *     operation may block waiting for a callback to finish.
 *
 * Results:
 *     VMCI_SUCCESS on success, appropriate error code otherwise.
 *
 * Side effects:
 *     May block.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_doorbell_destroy(struct vmci_handle handle)
{
	struct vmci_doorbell_entry *entry;
	struct vmci_resource *resource;
	int result;

	if (VMCI_HANDLE_INVALID(handle))
		return (VMCI_ERROR_INVALID_ARGS);

	resource = vmci_resource_get(handle, VMCI_RESOURCE_TYPE_DOORBELL);
	if (resource == NULL) {
		VMCI_LOG_DEBUG(LGPFX"Failed to destroy doorbell "
		    "(handle=0x%x:0x%x).\n", handle.context, handle.resource);
		return (VMCI_ERROR_NOT_FOUND);
	}
	entry = RESOURCE_CONTAINER(resource, struct vmci_doorbell_entry,
	    resource);

	vmci_doorbell_index_table_remove(entry);

	result = vmci_doorbell_unlink(handle, entry->is_doorbell);
	if (VMCI_SUCCESS != result) {

		/*
		 * The only reason this should fail would be an inconsistency
		 * between guest and hypervisor state, where the guest believes
		 * it has an active registration whereas the hypervisor doesn't.
		 * One case where this may happen is if a doorbell is
		 * unregistered following a hibernation at a time where the
		 * doorbell state hasn't been restored on the hypervisor side
		 * yet. Since the handle has now been removed in the guest,
		 * we just print a warning and return success.
		 */

		VMCI_LOG_DEBUG(LGPFX"Unlink of %s (handle=0x%x:0x%x) unknown "
		    "by hypervisor (error=%d).\n",
		    entry->is_doorbell ? "doorbell" : "queuepair",
		    handle.context, handle.resource, result);
	}

	/*
	 * Now remove the resource from the table.  It might still be in use
	 * after this, in a callback or still on the delayed work queue.
	 */

	vmci_resource_remove(handle, VMCI_RESOURCE_TYPE_DOORBELL);

	/*
	 * We now wait on the destroyEvent and release the reference we got
	 * above.
	 */

	vmci_wait_on_event(&entry->destroy_event, vmci_doorbell_release_cb,
	    entry);

	/*
	 * We know that we are now the only reference to the above entry so
	 * can safely free it.
	 */

	vmci_destroy_event(&entry->destroy_event);
	vmci_free_kernel_mem(entry, sizeof(*entry));

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_notify_as_guest --
 *
 *     Notify another guest or the host. We send a datagram down to the host
 *     via the hypervisor with the notification info.
 *
 * Results:
 *     VMCI_SUCCESS on success, appropriate error code otherwise.
 *
 * Side effects:
 *     May do a hypercall.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_doorbell_notify_as_guest(struct vmci_handle handle,
    vmci_privilege_flags priv_flags)
{
	struct vmci_doorbell_notify_msg notify_msg;

	notify_msg.hdr.dst = VMCI_MAKE_HANDLE(VMCI_HYPERVISOR_CONTEXT_ID,
	    VMCI_DOORBELL_NOTIFY);
	notify_msg.hdr.src = VMCI_ANON_SRC_HANDLE;
	notify_msg.hdr.payload_size = sizeof(notify_msg) - VMCI_DG_HEADERSIZE;
	notify_msg.handle = handle;

	return (vmci_send_datagram((struct vmci_datagram *)&notify_msg));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_notify --
 *
 *     Generates a notification on the doorbell identified by the handle. For
 *     host side generation of notifications, the caller can specify what the
 *     privilege of the calling side is.
 *
 * Results:
 *     VMCI_SUCCESS on success, appropriate error code otherwise.
 *
 * Side effects:
 *     May do a hypercall.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_doorbell_notify(struct vmci_handle dst, vmci_privilege_flags priv_flags)
{
	struct vmci_handle src;

	if (VMCI_HANDLE_INVALID(dst) ||
	    (priv_flags & ~VMCI_PRIVILEGE_ALL_FLAGS))
		return (VMCI_ERROR_INVALID_ARGS);

	src = VMCI_INVALID_HANDLE;

	return (vmci_doorbell_notify_as_guest(dst, priv_flags));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_delayed_dispatch_cb --
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
vmci_doorbell_delayed_dispatch_cb(void *data)
{
	struct vmci_doorbell_entry *entry = (struct vmci_doorbell_entry *)data;

	ASSERT(data);

	entry->notify_cb(entry->client_data);

	vmci_resource_release(&entry->resource);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_sync --
 *
 *     Use this as a synchronization point when setting globals, for example,
 *     during device shutdown.
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
vmci_doorbell_sync(void)
{

	vmci_grab_lock_bh(&vmci_doorbell_it.lock);
	vmci_release_lock_bh(&vmci_doorbell_it.lock);
	vmci_resource_sync();
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_register_notification_bitmap --
 *
 *     Register the notification bitmap with the host.
 *
 * Results:
 *     true if the bitmap is registered successfully with the device, false
 *     otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

bool
vmci_register_notification_bitmap(PPN bitmap_ppn)
{
	struct vmci_notify_bitmap_set_msg bitmap_set_msg;
	int result;

	/*
	 * Do not ASSERT() on the guest device here. This function can get
	 * called during device initialization, so the ASSERT() will fail even
	 * though the device is (almost) up.
	 */

	bitmap_set_msg.hdr.dst = VMCI_MAKE_HANDLE(VMCI_HYPERVISOR_CONTEXT_ID,
	    VMCI_SET_NOTIFY_BITMAP);
	bitmap_set_msg.hdr.src = VMCI_ANON_SRC_HANDLE;
	bitmap_set_msg.hdr.payload_size =
	    sizeof(bitmap_set_msg) - VMCI_DG_HEADERSIZE;
	bitmap_set_msg.bitmap_ppn = bitmap_ppn;

	result = vmci_send_datagram((struct vmci_datagram *)&bitmap_set_msg);
	if (result != VMCI_SUCCESS) {
		VMCI_LOG_DEBUG(LGPFX"Failed to register (PPN=%u) as "
		    "notification bitmap (error=%d).\n",
		    bitmap_ppn, result);
		return (false);
	}
	return (true);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_doorbell_fire_entries --
 *
 *     Executes or schedules the handlers for a given notify index.
 *
 * Result:
 *     Notification hash entry if found. NULL otherwise.
 *
 * Side effects:
 *     Whatever the side effects of the handlers are.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_doorbell_fire_entries(uint32_t notify_idx)
{
	struct vmci_doorbell_entry *iter;
	uint32_t bucket = VMCI_DOORBELL_HASH(notify_idx);

	vmci_grab_lock_bh(&vmci_doorbell_it.lock);

	vmci_list_scan(iter, &vmci_doorbell_it.entries[bucket], idx_list_item) {
		if (iter->idx == notify_idx &&
		    atomic_load_int(&iter->active) == 1) {
			ASSERT(iter->notify_cb);
			if (iter->run_delayed) {
				int err;

				vmci_resource_hold(&iter->resource);
				err = vmci_schedule_delayed_work(
				    vmci_doorbell_delayed_dispatch_cb, iter);
				if (err != VMCI_SUCCESS) {
					vmci_resource_release(&iter->resource);
					goto out;
				}
			} else
				iter->notify_cb(iter->client_data);
		}
	}

out:
	vmci_release_lock_bh(&vmci_doorbell_it.lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_scan_notification_bitmap --
 *
 *     Scans the notification bitmap, collects pending notifications, resets
 *     the bitmap and invokes appropriate callbacks.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     May schedule tasks, allocate memory and run callbacks.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_scan_notification_bitmap(uint8_t *bitmap)
{
	uint32_t idx;

	ASSERT(bitmap);

	for (idx = 0; idx < max_notify_idx; idx++) {
		if (bitmap[idx] & 0x1) {
			bitmap[idx] &= ~1;
			vmci_doorbell_fire_entries(idx);
		}
	}
}
