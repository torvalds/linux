// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#include <linux/sunrpc/clnt.h>
#include <linux/kobject.h>

static struct kset *rpc_sunrpc_kset;
static struct kobject *rpc_sunrpc_client_kobj;

static void rpc_sysfs_object_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct kobj_ns_type_operations *
rpc_sysfs_object_child_ns_type(struct kobject *kobj)
{
	return &net_ns_type_operations;
}

static struct kobj_type rpc_sysfs_object_type = {
	.release = rpc_sysfs_object_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.child_ns_type = rpc_sysfs_object_child_ns_type,
};

static struct kobject *rpc_sysfs_object_alloc(const char *name,
					      struct kset *kset,
					      struct kobject *parent)
{
	struct kobject *kobj;

	kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);
	if (kobj) {
		kobj->kset = kset;
		if (kobject_init_and_add(kobj, &rpc_sysfs_object_type,
					 parent, "%s", name) == 0)
			return kobj;
		kobject_put(kobj);
	}
	return NULL;
}

int rpc_sysfs_init(void)
{
	rpc_sunrpc_kset = kset_create_and_add("sunrpc", NULL, kernel_kobj);
	if (!rpc_sunrpc_kset)
		return -ENOMEM;
	rpc_sunrpc_client_kobj = rpc_sysfs_object_alloc("client", rpc_sunrpc_kset, NULL);
	if (!rpc_sunrpc_client_kobj) {
		kset_unregister(rpc_sunrpc_kset);
		rpc_sunrpc_client_kobj = NULL;
		return -ENOMEM;
	}
	return 0;
}

void rpc_sysfs_exit(void)
{
	kobject_put(rpc_sunrpc_client_kobj);
	kset_unregister(rpc_sunrpc_kset);
}
