/*
 * linux/ipc/namespace.c
 * Copyright (C) 2006 Pavel Emelyanov <xemul@openvz.org> OpenVZ, SWsoft Inc.
 */

#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/ipc_namespace.h>
#include <linux/rcupdate.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>

#include "util.h"

static struct ipc_namespace *clone_ipc_ns(struct ipc_namespace *old_ns)
{
	int err;
	struct ipc_namespace *ns;

	err = -ENOMEM;
	ns = kmalloc(sizeof(struct ipc_namespace), GFP_KERNEL);
	if (ns == NULL)
		goto err_mem;

	err = sem_init_ns(ns);
	if (err)
		goto err_sem;
	err = msg_init_ns(ns);
	if (err)
		goto err_msg;
	err = shm_init_ns(ns);
	if (err)
		goto err_shm;

	kref_init(&ns->kref);
	return ns;

err_shm:
	msg_exit_ns(ns);
err_msg:
	sem_exit_ns(ns);
err_sem:
	kfree(ns);
err_mem:
	return ERR_PTR(err);
}

struct ipc_namespace *copy_ipcs(unsigned long flags, struct ipc_namespace *ns)
{
	struct ipc_namespace *new_ns;

	BUG_ON(!ns);
	get_ipc_ns(ns);

	if (!(flags & CLONE_NEWIPC))
		return ns;

	new_ns = clone_ipc_ns(ns);

	put_ipc_ns(ns);
	return new_ns;
}

void free_ipc_ns(struct kref *kref)
{
	struct ipc_namespace *ns;

	ns = container_of(kref, struct ipc_namespace, kref);
	sem_exit_ns(ns);
	msg_exit_ns(ns);
	shm_exit_ns(ns);
	kfree(ns);
}
