/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 */

/* This file implements the VMCI Simple Datagram API on the host. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>

#include "vmci_datagram.h"
#include "vmci_driver.h"
#include "vmci_kernel_api.h"
#include "vmci_kernel_defs.h"
#include "vmci_resource.h"

#define LGPFX "vmci_datagram: "

/*
 * datagram_entry describes the datagram entity. It is used for datagram
 * entities created only on the host.
 */
struct datagram_entry {
	struct vmci_resource	resource;
	uint32_t		flags;
	bool			run_delayed;
	vmci_datagram_recv_cb	recv_cb;
	void			*client_data;
	vmci_event		destroy_event;
	vmci_privilege_flags	priv_flags;
};

struct vmci_delayed_datagram_info {
	struct datagram_entry	*entry;
	struct vmci_datagram	msg;
};

static int	vmci_datagram_get_priv_flags_int(vmci_id contextID,
		    struct vmci_handle handle,
		    vmci_privilege_flags *priv_flags);
static void	datagram_free_cb(void *resource);
static int	datagram_release_cb(void *client_data);

/*------------------------------ Helper functions ----------------------------*/

/*
 *------------------------------------------------------------------------------
 *
 * datagram_free_cb --
 *
 *     Callback to free datagram structure when resource is no longer used,
 *     ie. the reference count reached 0.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
datagram_free_cb(void *client_data)
{
	struct datagram_entry *entry = (struct datagram_entry *)client_data;

	ASSERT(entry);

	vmci_signal_event(&entry->destroy_event);

	/*
	 * The entry is freed in vmci_datagram_destroy_hnd, who is waiting for
	 * the above signal.
	 */
}

/*
 *------------------------------------------------------------------------------
 *
 * datagram_release_cb --
 *
 *     Callback to release the resource reference. It is called by the
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
datagram_release_cb(void *client_data)
{
	struct datagram_entry *entry;

	entry = (struct datagram_entry *)client_data;

	ASSERT(entry);

	vmci_resource_release(&entry->resource);

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * datagram_create_hnd --
 *
 *     Internal function to create a datagram entry given a handle.
 *
 * Results:
 *     VMCI_SUCCESS if created, negative errno value otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
datagram_create_hnd(vmci_id resource_id, uint32_t flags,
    vmci_privilege_flags priv_flags, vmci_datagram_recv_cb recv_cb,
    void *client_data, struct vmci_handle *out_handle)
{
	struct datagram_entry *entry;
	struct vmci_handle handle;
	vmci_id context_id;
	int result;

	ASSERT(recv_cb != NULL);
	ASSERT(out_handle != NULL);
	ASSERT(!(priv_flags & ~VMCI_PRIVILEGE_ALL_FLAGS));

	if ((flags & VMCI_FLAG_WELLKNOWN_DG_HND) != 0)
		return (VMCI_ERROR_INVALID_ARGS);
	else {
		if ((flags & VMCI_FLAG_ANYCID_DG_HND) != 0)
			context_id = VMCI_INVALID_ID;
		else {
			context_id = vmci_get_context_id();
			if (context_id == VMCI_INVALID_ID)
				return (VMCI_ERROR_NO_RESOURCES);
		}

		if (resource_id == VMCI_INVALID_ID) {
			resource_id = vmci_resource_get_id(context_id);
			if (resource_id == VMCI_INVALID_ID)
				return (VMCI_ERROR_NO_HANDLE);
		}

		handle = VMCI_MAKE_HANDLE(context_id, resource_id);
	}

	entry = vmci_alloc_kernel_mem(sizeof(*entry), VMCI_MEMORY_NORMAL);
	if (entry == NULL) {
		VMCI_LOG_WARNING(LGPFX"Failed allocating memory for datagram "
		    "entry.\n");
		return (VMCI_ERROR_NO_MEM);
	}

	if (!vmci_can_schedule_delayed_work()) {
		if (flags & VMCI_FLAG_DG_DELAYED_CB) {
			vmci_free_kernel_mem(entry, sizeof(*entry));
			return (VMCI_ERROR_INVALID_ARGS);
		}
		entry->run_delayed = false;
	} else
		entry->run_delayed = (flags & VMCI_FLAG_DG_DELAYED_CB) ?
		    true : false;

	entry->flags = flags;
	entry->recv_cb = recv_cb;
	entry->client_data = client_data;
	vmci_create_event(&entry->destroy_event);
	entry->priv_flags = priv_flags;

	/* Make datagram resource live. */
	result = vmci_resource_add(&entry->resource,
	    VMCI_RESOURCE_TYPE_DATAGRAM, handle, datagram_free_cb, entry);
	if (result != VMCI_SUCCESS) {
		VMCI_LOG_WARNING(LGPFX"Failed to add new resource "
		    "(handle=0x%x:0x%x).\n", handle.context, handle.resource);
		vmci_destroy_event(&entry->destroy_event);
		vmci_free_kernel_mem(entry, sizeof(*entry));
		return (result);
	}
	*out_handle = handle;

	return (VMCI_SUCCESS);
}

/*------------------------------ Public API functions ------------------------*/

/*
 *------------------------------------------------------------------------------
 *
 * vmci_datagram_create_handle --
 *
 *     Creates a host context datagram endpoint and returns a handle to it.
 *
 * Results:
 *     VMCI_SUCCESS if created, negative errno value otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_datagram_create_handle(vmci_id resource_id, uint32_t flags,
    vmci_datagram_recv_cb recv_cb, void *client_data,
    struct vmci_handle *out_handle)
{

	if (out_handle == NULL)
		return (VMCI_ERROR_INVALID_ARGS);

	if (recv_cb == NULL) {
		VMCI_LOG_DEBUG(LGPFX"Client callback needed when creating "
		    "datagram.\n");
		return (VMCI_ERROR_INVALID_ARGS);
	}

	return (datagram_create_hnd(resource_id, flags,
	    VMCI_DEFAULT_PROC_PRIVILEGE_FLAGS,
	    recv_cb, client_data, out_handle));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_datagram_create_handle_priv --
 *
 *     Creates a host context datagram endpoint and returns a handle to it.
 *
 * Results:
 *     VMCI_SUCCESS if created, negative errno value otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_datagram_create_handle_priv(vmci_id resource_id, uint32_t flags,
    vmci_privilege_flags priv_flags, vmci_datagram_recv_cb recv_cb,
    void *client_data, struct vmci_handle *out_handle)
{

	if (out_handle == NULL)
		return (VMCI_ERROR_INVALID_ARGS);

	if (recv_cb == NULL) {
		VMCI_LOG_DEBUG(LGPFX"Client callback needed when creating "
		    "datagram.\n");
		return (VMCI_ERROR_INVALID_ARGS);
	}

	if (priv_flags & ~VMCI_PRIVILEGE_ALL_FLAGS)
		return (VMCI_ERROR_INVALID_ARGS);

	return (datagram_create_hnd(resource_id, flags, priv_flags, recv_cb,
	    client_data, out_handle));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_datagram_destroy_handle --
 *
 *     Destroys a handle.
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
vmci_datagram_destroy_handle(struct vmci_handle handle)
{
	struct datagram_entry *entry;
	struct vmci_resource *resource;

	resource = vmci_resource_get(handle,
	    VMCI_RESOURCE_TYPE_DATAGRAM);
	if (resource == NULL) {
		VMCI_LOG_DEBUG(LGPFX"Failed to destroy datagram "
		    "(handle=0x%x:0x%x).\n", handle.context, handle.resource);
		return (VMCI_ERROR_NOT_FOUND);
	}
	entry = RESOURCE_CONTAINER(resource, struct datagram_entry, resource);

	vmci_resource_remove(handle, VMCI_RESOURCE_TYPE_DATAGRAM);

	/*
	 * We now wait on the destroyEvent and release the reference we got
	 * above.
	 */
	vmci_wait_on_event(&entry->destroy_event, datagram_release_cb, entry);

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
 *  vmci_datagram_get_priv_flags_int --
 *
 *      Internal utilility function with the same purpose as
 *      vmci_datagram_get_priv_flags that also takes a context_id.
 *
 *  Result:
 *      VMCI_SUCCESS on success, VMCI_ERROR_INVALID_ARGS if handle is invalid.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_datagram_get_priv_flags_int(vmci_id context_id, struct vmci_handle handle,
    vmci_privilege_flags *priv_flags)
{

	ASSERT(priv_flags);
	ASSERT(context_id != VMCI_INVALID_ID);

	if (context_id == VMCI_HOST_CONTEXT_ID) {
		struct datagram_entry *src_entry;
		struct vmci_resource *resource;

		resource = vmci_resource_get(handle,
		    VMCI_RESOURCE_TYPE_DATAGRAM);
		if (resource == NULL)
			return (VMCI_ERROR_INVALID_ARGS);
		src_entry = RESOURCE_CONTAINER(resource, struct datagram_entry,
		    resource);
		*priv_flags = src_entry->priv_flags;
		vmci_resource_release(resource);
	} else if (context_id == VMCI_HYPERVISOR_CONTEXT_ID)
		*priv_flags = VMCI_MAX_PRIVILEGE_FLAGS;
	else
		*priv_flags = VMCI_NO_PRIVILEGE_FLAGS;

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 *  vmci_datagram_fet_priv_flags --
 *
 *      Utility function that retrieves the privilege flags associated with a
 *      given datagram handle. For hypervisor and guest endpoints, the
 *      privileges are determined by the context ID, but for host endpoints
 *      privileges are associated with the complete handle.
 *
 *  Result:
 *      VMCI_SUCCESS on success, VMCI_ERROR_INVALID_ARGS if handle is invalid.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_datagram_get_priv_flags(struct vmci_handle handle,
    vmci_privilege_flags *priv_flags)
{

	if (priv_flags == NULL || handle.context == VMCI_INVALID_ID)
		return (VMCI_ERROR_INVALID_ARGS);

	return (vmci_datagram_get_priv_flags_int(handle.context, handle,
	    priv_flags));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_datagram_delayed_dispatch_cb --
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
vmci_datagram_delayed_dispatch_cb(void *data)
{
	struct vmci_delayed_datagram_info *dg_info;

	dg_info = (struct vmci_delayed_datagram_info *)data;

	ASSERT(data);

	dg_info->entry->recv_cb(dg_info->entry->client_data, &dg_info->msg);

	vmci_resource_release(&dg_info->entry->resource);

	vmci_free_kernel_mem(dg_info, sizeof(*dg_info) +
	    (size_t)dg_info->msg.payload_size);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_datagram_dispatch_as_guest --
 *
 *     Dispatch datagram as a guest, down through the VMX and potentially to
 *     the host.
 *
 * Result:
 *     Number of bytes sent on success, appropriate error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_datagram_dispatch_as_guest(struct vmci_datagram *dg)
{
	struct vmci_resource *resource;
	int retval;

	resource = vmci_resource_get(dg->src, VMCI_RESOURCE_TYPE_DATAGRAM);
	if (NULL == resource)
		return VMCI_ERROR_NO_HANDLE;

	retval = vmci_send_datagram(dg);
	vmci_resource_release(resource);

	return (retval);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_datagram_dispatch --
 *
 *     Dispatch datagram. This will determine the routing for the datagram and
 *     dispatch it accordingly.
 *
 * Result:
 *     Number of bytes sent on success, appropriate error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_datagram_dispatch(vmci_id context_id, struct vmci_datagram *dg)
{

	ASSERT(dg);
	ASSERT_ON_COMPILE(sizeof(struct vmci_datagram) == 24);

	if (VMCI_DG_SIZE(dg) > VMCI_MAX_DG_SIZE) {
		VMCI_LOG_DEBUG(LGPFX"Payload (size=%lu bytes) too big to send."
		    "\n", dg->payload_size);
		return (VMCI_ERROR_INVALID_ARGS);
	}

	return (vmci_datagram_dispatch_as_guest(dg));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_datagram_invoke_guest_handler --
 *
 *     Invoke the handler for the given datagram. This is intended to be called
 *     only when acting as a guest and receiving a datagram from the virtual
 *     device.
 *
 * Result:
 *     VMCI_SUCCESS on success, other error values on failure.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_datagram_invoke_guest_handler(struct vmci_datagram *dg)
{
	struct datagram_entry *dst_entry;
	struct vmci_resource *resource;
	int retval;

	ASSERT(dg);

	if (dg->payload_size > VMCI_MAX_DG_PAYLOAD_SIZE) {
		VMCI_LOG_DEBUG(LGPFX"Payload (size=%lu bytes) too large to "
		    "deliver.\n", dg->payload_size);
		return (VMCI_ERROR_PAYLOAD_TOO_LARGE);
	}

	resource = vmci_resource_get(dg->dst, VMCI_RESOURCE_TYPE_DATAGRAM);
	if (NULL == resource) {
		VMCI_LOG_DEBUG(LGPFX"destination (handle=0x%x:0x%x) doesn't "
		    "exist.\n", dg->dst.context, dg->dst.resource);
		return (VMCI_ERROR_NO_HANDLE);
	}

	dst_entry = RESOURCE_CONTAINER(resource, struct datagram_entry,
	    resource);
	if (dst_entry->run_delayed) {
		struct vmci_delayed_datagram_info *dg_info;

		dg_info = vmci_alloc_kernel_mem(sizeof(*dg_info) +
		    (size_t)dg->payload_size, VMCI_MEMORY_ATOMIC);
		if (NULL == dg_info) {
			vmci_resource_release(resource);
			retval = VMCI_ERROR_NO_MEM;
			goto exit;
		}

		dg_info->entry = dst_entry;
		memcpy(&dg_info->msg, dg, VMCI_DG_SIZE(dg));

		retval = vmci_schedule_delayed_work(
		    vmci_datagram_delayed_dispatch_cb, dg_info);
		if (retval < VMCI_SUCCESS) {
			VMCI_LOG_WARNING(LGPFX"Failed to schedule delayed "
			    "work for datagram (result=%d).\n", retval);
			vmci_free_kernel_mem(dg_info, sizeof(*dg_info) +
			    (size_t)dg->payload_size);
			vmci_resource_release(resource);
			dg_info = NULL;
			goto exit;
		}
	} else {
		dst_entry->recv_cb(dst_entry->client_data, dg);
		vmci_resource_release(resource);
		retval = VMCI_SUCCESS;
	}

exit:
	return (retval);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_datagram_send --
 *
 *     Sends the payload to the destination datagram handle.
 *
 * Results:
 *     Returns number of bytes sent if success, or error code if failure.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_datagram_send(struct vmci_datagram *msg)
{

	if (msg == NULL)
		return (VMCI_ERROR_INVALID_ARGS);

	return (vmci_datagram_dispatch(VMCI_INVALID_ID, msg));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_datagram_sync --
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
vmci_datagram_sync(void)
{

	vmci_resource_sync();
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_datagram_check_host_capabilities --
 *
 *     Verify that the host supports the resources we need. None are required
 *     for datagrams since they are implicitly supported.
 *
 * Results:
 *     true.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

bool
vmci_datagram_check_host_capabilities(void)
{

	return (true);
}
