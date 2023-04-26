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
struct gh_vm_function {
	u32 type;
	const char *name;
	struct module *mod;
	long (*bind)(struct gh_vm_function_instance *f);
	void (*unbind)(struct gh_vm_function_instance *f);
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

#define DECLARE_GH_VM_FUNCTION(_name, _type, _bind, _unbind)	\
	static struct gh_vm_function _name = {		\
		.type = _type,						\
		.name = __stringify(_name),				\
		.mod = THIS_MODULE,					\
		.bind = _bind,						\
		.unbind = _unbind,					\
	};								\
	MODULE_ALIAS("ghfunc:"__stringify(_type))

#define module_gh_vm_function(__gf)					\
	module_driver(__gf, gh_vm_function_register, gh_vm_function_unregister)

#define DECLARE_GH_VM_FUNCTION_INIT(_name, _type, _bind, _unbind)	\
	DECLARE_GH_VM_FUNCTION(_name, _type, _bind, _unbind);	\
	module_gh_vm_function(_name)

struct gh_vm_resource_ticket {
	struct list_head list; /* for gh_vm's resources list */
	struct list_head resources; /* for gh_resources's list */
	enum gh_resource_type resource_type;
	u32 label;

	struct module *owner;
	int (*populate)(struct gh_vm_resource_ticket *ticket, struct gh_resource *ghrsc);
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
