/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* Kernel API (v2) exported from the VMCI guest driver. */

#ifndef _VMCI_KERNEL_API_2_H_
#define _VMCI_KERNEL_API_2_H_

#include "vmci_kernel_api_1.h"

/* Define version 2. */

#undef  VMCI_KERNEL_API_VERSION
#define VMCI_KERNEL_API_VERSION_2	2
#define VMCI_KERNEL_API_VERSION		VMCI_KERNEL_API_VERSION_2

/* VMCI Doorbell API. */
#define VMCI_FLAG_DELAYED_CB		0x01

typedef void (*vmci_callback)(void *client_data);

int	vmci_doorbell_create(struct vmci_handle *handle, uint32_t flags,
	    vmci_privilege_flags priv_flags, vmci_callback notify_cb,
	    void *client_data);
int	vmci_doorbell_destroy(struct vmci_handle handle);
int	vmci_doorbell_notify(struct vmci_handle handle,
	    vmci_privilege_flags priv_flags);

#endif /* !_VMCI_KERNEL_API_2_H_ */
