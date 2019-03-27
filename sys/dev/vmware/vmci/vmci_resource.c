/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 */

/* Implementation of the VMCI Resource Access Control API. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "vmci_driver.h"
#include "vmci_kernel_defs.h"
#include "vmci_resource.h"

#define LGPFX	"vmci_resource: "

/* 0 through VMCI_RESERVED_RESOURCE_ID_MAX are reserved. */
static uint32_t resource_id = VMCI_RESERVED_RESOURCE_ID_MAX + 1;
static vmci_lock resource_id_lock;

static void	vmci_resource_do_remove(struct vmci_resource *resource);

static struct vmci_hashtable *resource_table = NULL;

/* Public Resource Access Control API. */

/*
 *------------------------------------------------------------------------------
 *
 * vmci_resource_init --
 *
 *     Initializes the VMCI Resource Access Control API. Creates a hashtable to
 *     hold all resources, and registers vectors and callbacks for hypercalls.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_resource_init(void)
{
	int err;

	err = vmci_init_lock(&resource_id_lock, "VMCI RID lock");
	if (err < VMCI_SUCCESS)
		return (err);

	resource_table = vmci_hashtable_create(128);
	if (resource_table == NULL) {
		VMCI_LOG_WARNING((LGPFX"Failed creating a resource hash table "
		    "for VMCI.\n"));
		vmci_cleanup_lock(&resource_id_lock);
		return (VMCI_ERROR_NO_MEM);
	}

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_resource_exit --
 *
 *      Cleans up resources.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_resource_exit(void)
{

	/* Cleanup resources.*/
	vmci_cleanup_lock(&resource_id_lock);

	if (resource_table)
		vmci_hashtable_destroy(resource_table);
}

/*
 *------------------------------------------------------------------------------
 *
 *  vmci_resource_get_id --
 *
 *      Return resource ID. The first VMCI_RESERVED_RESOURCE_ID_MAX are reserved
 *      so we start from its value + 1.
 *
 *  Result:
 *      VMCI resource id on success, VMCI_INVALID_ID on failure.
 *
 *  Side effects:
 *      None.
 *
 *
 *------------------------------------------------------------------------------
 */

vmci_id
vmci_resource_get_id(vmci_id context_id)
{
	vmci_id current_rid;
	vmci_id old_rid;
	bool found_rid;

	old_rid = resource_id;
	found_rid = false;

	/*
	 * Generate a unique resource ID. Keep on trying until we wrap around
	 * in the RID space.
	 */
	ASSERT(old_rid > VMCI_RESERVED_RESOURCE_ID_MAX);

	do {
		struct vmci_handle handle;

		vmci_grab_lock(&resource_id_lock);
		current_rid = resource_id;
		handle = VMCI_MAKE_HANDLE(context_id, current_rid);
		resource_id++;
		if (UNLIKELY(resource_id == VMCI_INVALID_ID)) {
			/* Skip the reserved rids. */
			resource_id = VMCI_RESERVED_RESOURCE_ID_MAX + 1;
		}
		vmci_release_lock(&resource_id_lock);
		found_rid = !vmci_hashtable_entry_exists(resource_table,
		    handle);
	} while (!found_rid && resource_id != old_rid);

	if (UNLIKELY(!found_rid))
		return (VMCI_INVALID_ID);
	else
		return (current_rid);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_resource_add --
 *
 *     Add resource to hashtable.
 *
 * Results:
 *     VMCI_SUCCESS if successful, error code if not.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_resource_add(struct vmci_resource *resource,
    vmci_resource_type resource_type, struct vmci_handle resource_handle,
    vmci_resource_free_cb container_free_cb, void *container_object)
{
	int result;

	ASSERT(resource);

	if (VMCI_HANDLE_EQUAL(resource_handle, VMCI_INVALID_HANDLE)) {
		VMCI_LOG_DEBUG(LGPFX"Invalid argument resource "
		    "(handle=0x%x:0x%x).\n", resource_handle.context,
		    resource_handle.resource);
		return (VMCI_ERROR_INVALID_ARGS);
	}

	vmci_hashtable_init_entry(&resource->hash_entry, resource_handle);
	resource->type = resource_type;
	resource->container_free_cb = container_free_cb;
	resource->container_object = container_object;

	/* Add resource to hashtable. */
	result = vmci_hashtable_add_entry(resource_table,
	    &resource->hash_entry);
	if (result != VMCI_SUCCESS) {
		VMCI_LOG_DEBUG(LGPFX"Failed to add entry to hash table "
		    "(result=%d).\n", result);
		return (result);
	}

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_resource_remove --
 *
 *     Remove resource from hashtable.
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
vmci_resource_remove(struct vmci_handle resource_handle,
    vmci_resource_type resource_type)
{
	struct vmci_resource *resource;

	resource = vmci_resource_get(resource_handle, resource_type);
	if (resource == NULL)
		return;

	/* Remove resource from hashtable. */
	vmci_hashtable_remove_entry(resource_table, &resource->hash_entry);

	vmci_resource_release(resource);
	/* resource could be freed by now. */
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_resource_get --
 *
 *     Get resource from hashtable.
 *
 * Results:
 *     Resource if successful. Otherwise NULL.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

struct vmci_resource *
vmci_resource_get(struct vmci_handle resource_handle,
    vmci_resource_type resource_type)
{
	struct vmci_hash_entry *entry;
	struct vmci_resource *resource;

	entry = vmci_hashtable_get_entry(resource_table, resource_handle);
	if (entry == NULL)
		return (NULL);
	resource = RESOURCE_CONTAINER(entry, struct vmci_resource, hash_entry);
	if (resource_type == VMCI_RESOURCE_TYPE_ANY ||
		resource->type == resource_type) {
		return (resource);
	}
	vmci_hashtable_release_entry(resource_table, entry);
	return (NULL);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_resource_hold --
 *
 *     Hold the given resource. This will hold the hashtable entry. This is like
 *     doing a Get() but without having to lookup the resource by handle.
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
vmci_resource_hold(struct vmci_resource *resource)
{

	ASSERT(resource);
	vmci_hashtable_hold_entry(resource_table, &resource->hash_entry);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_resource_do_remove --
 *
 *     Deallocates data structures associated with the given resource and
 *     invoke any call back registered for the resource.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     May deallocate memory and invoke a callback for the removed resource.
 *
 *------------------------------------------------------------------------------
 */

static void inline
vmci_resource_do_remove(struct vmci_resource *resource)
{

	ASSERT(resource);

	if (resource->container_free_cb) {
		resource->container_free_cb(resource->container_object);
		/* Resource has been freed don't dereference it. */
	}
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_resource_release --
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Resource's containerFreeCB will get called if last reference.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_resource_release(struct vmci_resource *resource)
{
	int result;

	ASSERT(resource);

	result = vmci_hashtable_release_entry(resource_table,
	    &resource->hash_entry);
	if (result == VMCI_SUCCESS_ENTRY_DEAD)
		vmci_resource_do_remove(resource);

	/*
	 * We propagate the information back to caller in case it wants to know
	 * whether entry was freed.
	 */
	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_resource_handle --
 *
 *     Get the handle for the given resource.
 *
 * Results:
 *     The resource's associated handle.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

struct vmci_handle
vmci_resource_handle(struct vmci_resource *resource)
{

	ASSERT(resource);
	return (resource->hash_entry.handle);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_resource_sync --
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
vmci_resource_sync(void)
{

	vmci_hashtable_sync(resource_table);
}
