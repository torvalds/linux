/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 */

/* VMCI initialization. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "vmci.h"
#include "vmci_doorbell.h"
#include "vmci_driver.h"
#include "vmci_event.h"
#include "vmci_kernel_api.h"
#include "vmci_kernel_defs.h"
#include "vmci_resource.h"

#define LGPFX			"vmci: "
#define VMCI_UTIL_NUM_RESOURCES	1

static vmci_id ctx_update_sub_id = VMCI_INVALID_ID;
static volatile int vm_context_id = VMCI_INVALID_ID;

/*
 *------------------------------------------------------------------------------
 *
 * vmci_util_cid_update --
 *
 *     Gets called with the new context id if updated or resumed.
 *
 * Results:
 *     Context id.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_util_cid_update(vmci_id sub_id, struct vmci_event_data *event_data,
    void *client_data)
{
	struct vmci_event_payload_context *ev_payload;

	ev_payload = vmci_event_data_payload(event_data);

	if (sub_id != ctx_update_sub_id) {
		VMCI_LOG_DEBUG(LGPFX"Invalid subscriber (ID=0x%x).\n", sub_id);
		return;
	}
	if (event_data == NULL || ev_payload->context_id == VMCI_INVALID_ID) {
		VMCI_LOG_DEBUG(LGPFX"Invalid event data.\n");
		return;
	}
	VMCI_LOG_INFO(LGPFX"Updating context from (ID=0x%x) to (ID=0x%x) on "
	    "event (type=%d).\n", atomic_load_int(&vm_context_id),
	    ev_payload->context_id, event_data->event);
	atomic_store_int(&vm_context_id, ev_payload->context_id);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_util_init --
 *
 *     Subscribe to context id update event.
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
vmci_util_init(void)
{

	/*
	 * We subscribe to the VMCI_EVENT_CTX_ID_UPDATE here so we can update
	 * the internal context id when needed.
	 */
	if (vmci_event_subscribe(VMCI_EVENT_CTX_ID_UPDATE,
	    vmci_util_cid_update, NULL, &ctx_update_sub_id) < VMCI_SUCCESS) {
		VMCI_LOG_WARNING(LGPFX"Failed to subscribe to event "
		    "(type=%d).\n", VMCI_EVENT_CTX_ID_UPDATE);
	}
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_util_exit --
 *
 *     Cleanup
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
vmci_util_exit(void)
{

	if (vmci_event_unsubscribe(ctx_update_sub_id) < VMCI_SUCCESS)
		VMCI_LOG_WARNING(LGPFX"Failed to unsubscribe to event "
		    "(type=%d) with subscriber (ID=0x%x).\n",
		    VMCI_EVENT_CTX_ID_UPDATE, ctx_update_sub_id);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_util_check_host_capabilities --
 *
 *     Verify that the host supports the hypercalls we need. If it does not, try
 *     to find fallback hypercalls and use those instead.
 *
 * Results:
 *     true if required hypercalls (or fallback hypercalls) are supported by the
 *     host, false otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static bool
vmci_util_check_host_capabilities(void)
{
	struct vmci_resources_query_msg *msg;
	struct vmci_datagram *check_msg;
	int result;
	uint32_t msg_size;

	msg_size = sizeof(struct vmci_resources_query_hdr) +
	    VMCI_UTIL_NUM_RESOURCES * sizeof(vmci_resource);
	check_msg = vmci_alloc_kernel_mem(msg_size, VMCI_MEMORY_NORMAL);

	if (check_msg == NULL) {
		VMCI_LOG_WARNING(LGPFX"Check host: Insufficient memory.\n");
		return (false);
	}

	check_msg->dst = VMCI_MAKE_HANDLE(VMCI_HYPERVISOR_CONTEXT_ID,
	    VMCI_RESOURCES_QUERY);
	check_msg->src = VMCI_ANON_SRC_HANDLE;
	check_msg->payload_size = msg_size - VMCI_DG_HEADERSIZE;
	msg = (struct vmci_resources_query_msg *)VMCI_DG_PAYLOAD(check_msg);

	msg->num_resources = VMCI_UTIL_NUM_RESOURCES;
	msg->resources[0] = VMCI_GET_CONTEXT_ID;

	result = vmci_send_datagram(check_msg);
	vmci_free_kernel_mem(check_msg, msg_size);

	/* We need the vector. There are no fallbacks. */
	return (result == 0x1);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_check_host_capabilities --
 *
 *     Tell host which guestcalls we support and let each API check that the
 *     host supports the hypercalls it needs. If a hypercall is not supported,
 *     the API can check for a fallback hypercall, or fail the check.
 *
 * Results:
 *     true if successful, false otherwise.
 *
 * Side effects:
 *     Fallback mechanisms may be enabled in the API and vmmon.
 *
 *------------------------------------------------------------------------------
 */

bool
vmci_check_host_capabilities(void)
{
	bool result;

	result = vmci_event_check_host_capabilities();
	result &= vmci_datagram_check_host_capabilities();
	result &= vmci_util_check_host_capabilities();

	if (!result) {
		/*
		 * If it failed, then make sure this goes to the system event
		 * log.
		 */
		VMCI_LOG_WARNING(LGPFX"Host capability checked failed.\n");
	} else
		VMCI_LOG_DEBUG(LGPFX"Host capability check passed.\n");

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_read_datagrams_from_port --
 *
 *     Reads datagrams from the data in port and dispatches them. We always
 *     start reading datagrams into only the first page of the datagram buffer.
 *     If the datagrams don't fit into one page, we use the maximum datagram
 *     buffer size for the remainder of the  invocation. This is a simple
 *     heuristic for not penalizing small datagrams.
 *
 *     This function assumes that it has exclusive access to the data in port
 *     for the duration of the call.
 *
 * Results:
 *     No result.
 *
 * Side effects:
 *     Datagram handlers may be invoked.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_read_datagrams_from_port(vmci_io_handle io_handle, vmci_io_port dg_in_port,
    uint8_t *dg_in_buffer, size_t dg_in_buffer_size)
{
	struct vmci_datagram *dg;
	size_t current_dg_in_buffer_size;
	size_t remaining_bytes;

	current_dg_in_buffer_size = PAGE_SIZE;

	ASSERT(dg_in_buffer_size >= PAGE_SIZE);

	vmci_read_port_bytes(io_handle, dg_in_port, dg_in_buffer,
	    current_dg_in_buffer_size);
	dg = (struct vmci_datagram *)dg_in_buffer;
	remaining_bytes = current_dg_in_buffer_size;

	while (dg->dst.resource != VMCI_INVALID_ID ||
	    remaining_bytes > PAGE_SIZE) {
		size_t dg_in_size;

		/*
		 * When the input buffer spans multiple pages, a datagram can
		 * start on any page boundary in the buffer.
		 */

		if (dg->dst.resource == VMCI_INVALID_ID) {
			ASSERT(remaining_bytes > PAGE_SIZE);
			dg = (struct vmci_datagram *)ROUNDUP((uintptr_t)dg + 1,
			    PAGE_SIZE);
			ASSERT((uint8_t *)dg < dg_in_buffer +
			    current_dg_in_buffer_size);
			remaining_bytes = (size_t)(dg_in_buffer +
			    current_dg_in_buffer_size - (uint8_t *)dg);
			continue;
		}

		dg_in_size = VMCI_DG_SIZE_ALIGNED(dg);

		if (dg_in_size <= dg_in_buffer_size) {
			int result;

			/*
			 * If the remaining bytes in the datagram buffer doesn't
			 * contain the complete datagram, we first make sure we
			 * have enough room for it and then we read the reminder
			 * of the datagram and possibly any following datagrams.
			 */

			if (dg_in_size > remaining_bytes) {

				if (remaining_bytes !=
				    current_dg_in_buffer_size) {

					/*
					 * We move the partial datagram to the
					 * front and read the reminder of the
					 * datagram and possibly following calls
					 * into the following bytes.
					 */

					memmove(dg_in_buffer, dg_in_buffer +
					    current_dg_in_buffer_size -
					    remaining_bytes,
					    remaining_bytes);

					dg = (struct vmci_datagram *)
					    dg_in_buffer;
				}

				if (current_dg_in_buffer_size !=
				    dg_in_buffer_size)
					current_dg_in_buffer_size =
					    dg_in_buffer_size;

				vmci_read_port_bytes(io_handle, dg_in_port,
				    dg_in_buffer + remaining_bytes,
				    current_dg_in_buffer_size -
				    remaining_bytes);
			}

			/*
			 * We special case event datagrams from the
			 * hypervisor.
			 */
			if (dg->src.context == VMCI_HYPERVISOR_CONTEXT_ID &&
			    dg->dst.resource == VMCI_EVENT_HANDLER)
				result = vmci_event_dispatch(dg);
			else
				result =
				    vmci_datagram_invoke_guest_handler(dg);
			if (result < VMCI_SUCCESS)
				VMCI_LOG_DEBUG(LGPFX"Datagram with resource"
				    " (ID=0x%x) failed (err=%d).\n",
				    dg->dst.resource, result);

			/* On to the next datagram. */
			dg = (struct vmci_datagram *)((uint8_t *)dg +
			    dg_in_size);
		} else {
			size_t bytes_to_skip;

			/*
			 * Datagram doesn't fit in datagram buffer of maximal
			 * size. We drop it.
			 */

			VMCI_LOG_DEBUG(LGPFX"Failed to receive datagram "
			    "(size=%zu bytes).\n", dg_in_size);

			bytes_to_skip = dg_in_size - remaining_bytes;
			if (current_dg_in_buffer_size != dg_in_buffer_size)
				current_dg_in_buffer_size = dg_in_buffer_size;
			for (;;) {
				vmci_read_port_bytes(io_handle, dg_in_port,
				    dg_in_buffer, current_dg_in_buffer_size);
				if (bytes_to_skip <=
				    current_dg_in_buffer_size)
					break;
				bytes_to_skip -= current_dg_in_buffer_size;
			}
			dg = (struct vmci_datagram *)(dg_in_buffer +
			    bytes_to_skip);
		}

		remaining_bytes = (size_t) (dg_in_buffer +
		    current_dg_in_buffer_size - (uint8_t *)dg);

		if (remaining_bytes < VMCI_DG_HEADERSIZE) {
			/* Get the next batch of datagrams. */

			vmci_read_port_bytes(io_handle, dg_in_port,
			    dg_in_buffer, current_dg_in_buffer_size);
			dg = (struct vmci_datagram *)dg_in_buffer;
			remaining_bytes = current_dg_in_buffer_size;
		}
	}
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_get_context_id --
 *
 *     Returns the current context ID.  Note that since this is accessed only
 *     from code running in the host, this always returns the host context ID.
 *
 * Results:
 *     Context ID.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

vmci_id
vmci_get_context_id(void)
{
	if (atomic_load_int(&vm_context_id) == VMCI_INVALID_ID) {
		uint32_t result;
		struct vmci_datagram get_cid_msg;
		get_cid_msg.dst =  VMCI_MAKE_HANDLE(VMCI_HYPERVISOR_CONTEXT_ID,
		    VMCI_GET_CONTEXT_ID);
		get_cid_msg.src = VMCI_ANON_SRC_HANDLE;
		get_cid_msg.payload_size = 0;
		result = vmci_send_datagram(&get_cid_msg);
		atomic_store_int(&vm_context_id, result);
	}
	return (atomic_load_int(&vm_context_id));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_components_init --
 *
 *     Initializes VMCI components and registers core hypercalls.
 *
 * Results:
 *     VMCI_SUCCESS if successful, appropriate error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_components_init(void)
{
	int result;

	result = vmci_resource_init();
	if (result < VMCI_SUCCESS) {
		VMCI_LOG_WARNING(LGPFX"Failed to initialize vmci_resource "
		    "(result=%d).\n", result);
		goto error_exit;
	}

	result = vmci_event_init();
	if (result < VMCI_SUCCESS) {
		VMCI_LOG_WARNING(LGPFX"Failed to initialize vmci_event "
		    "(result=%d).\n", result);
		goto resource_exit;
	}

	result = vmci_doorbell_init();
	if (result < VMCI_SUCCESS) {
		VMCI_LOG_WARNING(LGPFX"Failed to initialize vmci_doorbell "
		    "(result=%d).\n", result);
		goto event_exit;
	}

	VMCI_LOG_DEBUG(LGPFX"components initialized.\n");
	return (VMCI_SUCCESS);

event_exit:
	vmci_event_exit();

resource_exit:
	vmci_resource_exit();

error_exit:
	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_components_cleanup --
 *
 *     Cleans up VMCI components.
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
vmci_components_cleanup(void)
{

	vmci_doorbell_exit();
	vmci_event_exit();
	vmci_resource_exit();
}
