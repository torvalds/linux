/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GUNYAH_VM_MGR_H
#define _GUNYAH_VM_MGR_H

#include <linux/compiler_types.h>
#include <linux/gunyah.h>
#include <linux/gunyah_rsc_mgr.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/notifier.h>

#include <uapi/linux/gunyah.h>

struct gh_vm;

int __must_check gh_vm_get(struct gh_vm *ghvm);
void gh_vm_put(struct gh_vm *ghvm);

struct gh_vm_function_instance;
/**
 * struct gh_vm_function - Represents a function type
 * @type: value from &enum gh_fn_type
 * @name: friendly name for debug purposes
 * @mod: owner of the function type
 * @bind: Called when a new function of this type has been allocated.
 * @unbind: Called when the function instance is being destroyed.
 * @compare: Compare function instance @f's argument to the provided arg.
 *           Return true if they are equivalent. Used on GH_VM_REMOVE_FUNCTION.
 */
struct gh_vm_function {
	u32 type;
	const char *name;
	struct module *mod;
	long (*bind)(struct gh_vm_function_instance *f);
	void (*unbind)(struct gh_vm_function_instance *f);
	bool (*compare)(const struct gh_vm_function_instance *f, const void *arg, size_t size);
};

/**
 * struct gh_vm_function_instance - Represents one function instance
 * @arg_size: size of user argument
 * @argp: pointer to user argument
 * @ghvm: Pointer to VM instance
 * @rm: Pointer to resource manager for the VM instance
 * @fn: The ops for the function
 * @data: Private data for function
 * @vm_list: for gh_vm's functions list
 * @fn_list: for gh_vm_function's instances list
 */
struct gh_vm_function_instance {
	size_t arg_size;
	void *argp;
	struct gh_vm *ghvm;
	struct gh_rm *rm;
	struct gh_vm_function *fn;
	void *data;
	struct list_head vm_list;
};

int gh_vm_function_register(struct gh_vm_function *f);
void gh_vm_function_unregister(struct gh_vm_function *f);

/* Since the function identifiers were setup in a uapi header as an
 * enum and we do no want to change that, the user must supply the expanded
 * constant as well and the compiler checks they are the same.
 * See also MODULE_ALIAS_RDMA_NETLINK.
 */
#define MODULE_ALIAS_GH_VM_FUNCTION(_type, _idx)			\
	static inline void __maybe_unused __chk##_idx(void)		\
	{								\
		BUILD_BUG_ON(_type != _idx);				\
	}								\
	MODULE_ALIAS("ghfunc:" __stringify(_idx))

#define DECLARE_GH_VM_FUNCTION(_name, _type, _bind, _unbind, _compare)	\
	static struct gh_vm_function _name = {				\
		.type = _type,						\
		.name = __stringify(_name),				\
		.mod = THIS_MODULE,					\
		.bind = _bind,						\
		.unbind = _unbind,					\
		.compare = _compare,					\
	}

#define module_gh_vm_function(__gf)					\
	module_driver(__gf, gh_vm_function_register, gh_vm_function_unregister)

#define DECLARE_GH_VM_FUNCTION_INIT(_name, _type, _idx, _bind, _unbind, _compare)	\
	DECLARE_GH_VM_FUNCTION(_name, _type, _bind, _unbind, _compare);			\
	module_gh_vm_function(_name);							\
	MODULE_ALIAS_GH_VM_FUNCTION(_type, _idx)

/**
 * struct gh_vm_resource_ticket - Represents a ticket to reserve exclusive access to VM resource(s)
 * @vm_list: for @gh_vm->resource_tickets
 * @resources: List of resource(s) associated with this ticket(members are from @gh_resource->list)
 * @resource_type: Type of resource this ticket reserves
 * @label: Label of the resource from resource manager this ticket reserves.
 * @owner: owner of the ticket
 * @populate: callback provided by the ticket owner and called when a resource is found that
 *            matches @resource_type and @label. Note that this callback could be called
 *            multiple times if userspace created mutliple resources with the same type/label.
 *            This callback may also have significant delay after gh_vm_add_resource_ticket()
 *            since gh_vm_add_resource_ticket() could be called before the VM starts.
 * @unpopulate: callback provided by the ticket owner and called when the ticket owner should no
 *              no longer use the resource provided in the argument. When unpopulate() returns,
 *              the ticket owner should not be able to use the resource any more as the resource
 *              might being freed.
 */
struct gh_vm_resource_ticket {
	struct list_head vm_list;
	struct list_head resources;
	enum gh_resource_type resource_type;
	u32 label;

	struct module *owner;
	bool (*populate)(struct gh_vm_resource_ticket *ticket, struct gh_resource *ghrsc);
	void (*unpopulate)(struct gh_vm_resource_ticket *ticket, struct gh_resource *ghrsc);
};

int gh_vm_add_resource_ticket(struct gh_vm *ghvm, struct gh_vm_resource_ticket *ticket);
void gh_vm_remove_resource_ticket(struct gh_vm *ghvm, struct gh_vm_resource_ticket *ticket);

/*
 * gh_vm_io_handler contains the info about an io device and its associated
 * addr and the ops associated with the io device.
 */
struct gh_vm_io_handler {
	struct rb_node node;
	u64 addr;

	bool datamatch;
	u8 len;
	u64 data;
	struct gh_vm_io_handler_ops *ops;
};

/*
 * gh_vm_io_handler_ops contains function pointers associated with an iodevice.
 */
struct gh_vm_io_handler_ops {
	int (*read)(struct gh_vm_io_handler *io_dev, u64 addr, u32 len, u64 data);
	int (*write)(struct gh_vm_io_handler *io_dev, u64 addr, u32 len, u64 data);
};

int gh_vm_add_io_handler(struct gh_vm *ghvm, struct gh_vm_io_handler *io_dev);
void gh_vm_remove_io_handler(struct gh_vm *ghvm, struct gh_vm_io_handler *io_dev);

#endif
