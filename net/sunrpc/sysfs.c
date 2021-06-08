// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#include <linux/sunrpc/clnt.h>
#include <linux/kobject.h>
#include "sysfs.h"

static struct kset *rpc_sunrpc_kset;
static struct kobject *rpc_sunrpc_client_kobj, *rpc_sunrpc_xprt_switch_kobj;

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
	rpc_sunrpc_client_kobj =
		rpc_sysfs_object_alloc("rpc-clients", rpc_sunrpc_kset, NULL);
	if (!rpc_sunrpc_client_kobj)
		goto err_client;
	rpc_sunrpc_xprt_switch_kobj =
		rpc_sysfs_object_alloc("xprt-switches", rpc_sunrpc_kset, NULL);
	if (!rpc_sunrpc_xprt_switch_kobj)
		goto err_switch;
	return 0;
err_switch:
	kobject_put(rpc_sunrpc_client_kobj);
	rpc_sunrpc_client_kobj = NULL;
err_client:
	kset_unregister(rpc_sunrpc_kset);
	rpc_sunrpc_kset = NULL;
	return -ENOMEM;
}

static void rpc_sysfs_client_release(struct kobject *kobj)
{
	struct rpc_sysfs_client *c;

	c = container_of(kobj, struct rpc_sysfs_client, kobject);
	kfree(c);
}

static void rpc_sysfs_xprt_switch_release(struct kobject *kobj)
{
	struct rpc_sysfs_xprt_switch *xprt_switch;

	xprt_switch = container_of(kobj, struct rpc_sysfs_xprt_switch, kobject);
	kfree(xprt_switch);
}

static const void *rpc_sysfs_client_namespace(struct kobject *kobj)
{
	return container_of(kobj, struct rpc_sysfs_client, kobject)->net;
}

static const void *rpc_sysfs_xprt_switch_namespace(struct kobject *kobj)
{
	return container_of(kobj, struct rpc_sysfs_xprt_switch, kobject)->net;
}

static struct kobj_type rpc_sysfs_client_type = {
	.release = rpc_sysfs_client_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.namespace = rpc_sysfs_client_namespace,
};

static struct kobj_type rpc_sysfs_xprt_switch_type = {
	.release = rpc_sysfs_xprt_switch_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.namespace = rpc_sysfs_xprt_switch_namespace,
};

void rpc_sysfs_exit(void)
{
	kobject_put(rpc_sunrpc_client_kobj);
	kobject_put(rpc_sunrpc_xprt_switch_kobj);
	kset_unregister(rpc_sunrpc_kset);
}

static struct rpc_sysfs_client *rpc_sysfs_client_alloc(struct kobject *parent,
						       struct net *net,
						       int clid)
{
	struct rpc_sysfs_client *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (p) {
		p->net = net;
		p->kobject.kset = rpc_sunrpc_kset;
		if (kobject_init_and_add(&p->kobject, &rpc_sysfs_client_type,
					 parent, "clnt-%d", clid) == 0)
			return p;
		kobject_put(&p->kobject);
	}
	return NULL;
}

static struct rpc_sysfs_xprt_switch *
rpc_sysfs_xprt_switch_alloc(struct kobject *parent,
			    struct rpc_xprt_switch *xprt_switch,
			    struct net *net,
			    gfp_t gfp_flags)
{
	struct rpc_sysfs_xprt_switch *p;

	p = kzalloc(sizeof(*p), gfp_flags);
	if (p) {
		p->net = net;
		p->kobject.kset = rpc_sunrpc_kset;
		if (kobject_init_and_add(&p->kobject,
					 &rpc_sysfs_xprt_switch_type,
					 parent, "switch-%d",
					 xprt_switch->xps_id) == 0)
			return p;
		kobject_put(&p->kobject);
	}
	return NULL;
}

void rpc_sysfs_client_setup(struct rpc_clnt *clnt, struct net *net)
{
	struct rpc_sysfs_client *rpc_client;

	rpc_client = rpc_sysfs_client_alloc(rpc_sunrpc_client_kobj, net, clnt->cl_clid);
	if (rpc_client) {
		clnt->cl_sysfs = rpc_client;
		kobject_uevent(&rpc_client->kobject, KOBJ_ADD);
	}
}

void rpc_sysfs_xprt_switch_setup(struct rpc_xprt_switch *xprt_switch,
				 struct rpc_xprt *xprt,
				 gfp_t gfp_flags)
{
	struct rpc_sysfs_xprt_switch *rpc_xprt_switch;
	struct net *net;

	if (xprt_switch->xps_net)
		net = xprt_switch->xps_net;
	else
		net = xprt->xprt_net;
	rpc_xprt_switch =
		rpc_sysfs_xprt_switch_alloc(rpc_sunrpc_xprt_switch_kobj,
					    xprt_switch, net, gfp_flags);
	if (rpc_xprt_switch) {
		xprt_switch->xps_sysfs = rpc_xprt_switch;
		rpc_xprt_switch->xprt_switch = xprt_switch;
		rpc_xprt_switch->xprt = xprt;
		kobject_uevent(&rpc_xprt_switch->kobject, KOBJ_ADD);
	}
}

void rpc_sysfs_client_destroy(struct rpc_clnt *clnt)
{
	struct rpc_sysfs_client *rpc_client = clnt->cl_sysfs;

	if (rpc_client) {
		kobject_uevent(&rpc_client->kobject, KOBJ_REMOVE);
		kobject_del(&rpc_client->kobject);
		kobject_put(&rpc_client->kobject);
		clnt->cl_sysfs = NULL;
	}
}

void rpc_sysfs_xprt_switch_destroy(struct rpc_xprt_switch *xprt_switch)
{
	struct rpc_sysfs_xprt_switch *rpc_xprt_switch = xprt_switch->xps_sysfs;

	if (rpc_xprt_switch) {
		kobject_uevent(&rpc_xprt_switch->kobject, KOBJ_REMOVE);
		kobject_del(&rpc_xprt_switch->kobject);
		kobject_put(&rpc_xprt_switch->kobject);
		xprt_switch->xps_sysfs = NULL;
	}
}
