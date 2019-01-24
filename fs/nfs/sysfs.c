// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Hammerspace Inc
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/netdevice.h>

#include "sysfs.h"

struct kobject *nfs_client_kobj;
static struct kset *nfs_client_kset;

static void nfs_netns_object_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct kobj_ns_type_operations *nfs_netns_object_child_ns_type(
		struct kobject *kobj)
{
	return &net_ns_type_operations;
}

static struct kobj_type nfs_netns_object_type = {
	.release = nfs_netns_object_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.child_ns_type = nfs_netns_object_child_ns_type,
};

static struct kobject *nfs_netns_object_alloc(const char *name,
		struct kset *kset, struct kobject *parent)
{
	struct kobject *kobj;

	kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);
	if (kobj) {
		kobj->kset = kset;
		if (kobject_init_and_add(kobj, &nfs_netns_object_type,
					parent, "%s", name) == 0)
			return kobj;
		kobject_put(kobj);
	}
	return NULL;
}

int nfs_sysfs_init(void)
{
	nfs_client_kset = kset_create_and_add("nfs", NULL, fs_kobj);
	if (!nfs_client_kset)
		return -ENOMEM;
	nfs_client_kobj = nfs_netns_object_alloc("net", nfs_client_kset, NULL);
	if  (!nfs_client_kobj) {
		kset_unregister(nfs_client_kset);
		nfs_client_kset = NULL;
		return -ENOMEM;
	}
	return 0;
}

void nfs_sysfs_exit(void)
{
	kobject_put(nfs_client_kobj);
	kset_unregister(nfs_client_kset);
}
