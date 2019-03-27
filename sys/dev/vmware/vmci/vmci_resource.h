/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* VMCI Resource Access Control API. */

#ifndef _VMCI_RESOURCE_H_
#define _VMCI_RESOURCE_H_

#include "vmci_defs.h"
#include "vmci_hashtable.h"
#include "vmci_kernel_if.h"

#define RESOURCE_CONTAINER(ptr, type, member)				\
	((type *)((char *)(ptr) - offsetof(type, member)))

typedef void(*vmci_resource_free_cb)(void *resource);

typedef enum {
	VMCI_RESOURCE_TYPE_ANY,
	VMCI_RESOURCE_TYPE_API,
	VMCI_RESOURCE_TYPE_GROUP,
	VMCI_RESOURCE_TYPE_DATAGRAM,
	VMCI_RESOURCE_TYPE_DOORBELL,
} vmci_resource_type;

struct vmci_resource {
	struct vmci_hash_entry	hash_entry;
	vmci_resource_type	type;
	/* Callback to free container object when refCount is 0. */
	vmci_resource_free_cb	container_free_cb;
	/* Container object reference. */
	void			*container_object;
};

int	vmci_resource_init(void);
void	vmci_resource_exit(void);
void	vmci_resource_sync(void);

vmci_id	vmci_resource_get_id(vmci_id context_id);

int	vmci_resource_add(struct vmci_resource *resource,
	    vmci_resource_type resource_type,
	    struct vmci_handle resource_handle,
	    vmci_resource_free_cb container_free_cb, void *container_object);
void	vmci_resource_remove(struct vmci_handle resource_handle,
	    vmci_resource_type resource_type);
struct	vmci_resource *vmci_resource_get(struct vmci_handle resource_handle,
	    vmci_resource_type resource_type);
void	vmci_resource_hold(struct vmci_resource *resource);
int	vmci_resource_release(struct vmci_resource *resource);
struct	vmci_handle vmci_resource_handle(struct vmci_resource *resource);

#endif /* !_VMCI_RESOURCE_H_ */
