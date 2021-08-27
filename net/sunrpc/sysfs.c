// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#include <linux/sunrpc/clnt.h>
#include <linux/kobject.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/xprtsock.h>

#include "sysfs.h"

struct xprt_addr {
	const char *addr;
	struct rcu_head rcu;
};

static void free_xprt_addr(struct rcu_head *head)
{
	struct xprt_addr *addr = container_of(head, struct xprt_addr, rcu);

	kfree(addr->addr);
	kfree(addr);
}

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

static inline struct rpc_xprt *
rpc_sysfs_xprt_kobj_get_xprt(struct kobject *kobj)
{
	struct rpc_sysfs_xprt *x = container_of(kobj,
		struct rpc_sysfs_xprt, kobject);

	return xprt_get(x->xprt);
}

static inline struct rpc_xprt_switch *
rpc_sysfs_xprt_kobj_get_xprt_switch(struct kobject *kobj)
{
	struct rpc_sysfs_xprt *x = container_of(kobj,
		struct rpc_sysfs_xprt, kobject);

	return xprt_switch_get(x->xprt_switch);
}

static inline struct rpc_xprt_switch *
rpc_sysfs_xprt_switch_kobj_get_xprt(struct kobject *kobj)
{
	struct rpc_sysfs_xprt_switch *x = container_of(kobj,
		struct rpc_sysfs_xprt_switch, kobject);

	return xprt_switch_get(x->xprt_switch);
}

static ssize_t rpc_sysfs_xprt_dstaddr_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
	struct rpc_xprt *xprt = rpc_sysfs_xprt_kobj_get_xprt(kobj);
	ssize_t ret;

	if (!xprt)
		return 0;
	ret = sprintf(buf, "%s\n", xprt->address_strings[RPC_DISPLAY_ADDR]);
	xprt_put(xprt);
	return ret + 1;
}

static ssize_t rpc_sysfs_xprt_srcaddr_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
	struct rpc_xprt *xprt = rpc_sysfs_xprt_kobj_get_xprt(kobj);
	struct sockaddr_storage saddr;
	struct sock_xprt *sock;
	ssize_t ret = -1;

	if (!xprt)
		return 0;

	sock = container_of(xprt, struct sock_xprt, xprt);
	if (kernel_getsockname(sock->sock, (struct sockaddr *)&saddr) < 0)
		goto out;

	ret = sprintf(buf, "%pISc\n", &saddr);
out:
	xprt_put(xprt);
	return ret + 1;
}

static ssize_t rpc_sysfs_xprt_info_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	struct rpc_xprt *xprt = rpc_sysfs_xprt_kobj_get_xprt(kobj);
	ssize_t ret;

	if (!xprt)
		return 0;

	ret = sprintf(buf, "last_used=%lu\ncur_cong=%lu\ncong_win=%lu\n"
		       "max_num_slots=%u\nmin_num_slots=%u\nnum_reqs=%u\n"
		       "binding_q_len=%u\nsending_q_len=%u\npending_q_len=%u\n"
		       "backlog_q_len=%u\nmain_xprt=%d\nsrc_port=%u\n"
		       "tasks_queuelen=%ld\ndst_port=%s\n",
		       xprt->last_used, xprt->cong, xprt->cwnd, xprt->max_reqs,
		       xprt->min_reqs, xprt->num_reqs, xprt->binding.qlen,
		       xprt->sending.qlen, xprt->pending.qlen,
		       xprt->backlog.qlen, xprt->main,
		       (xprt->xprt_class->ident == XPRT_TRANSPORT_TCP) ?
		       get_srcport(xprt) : 0,
		       atomic_long_read(&xprt->queuelen),
		       (xprt->xprt_class->ident == XPRT_TRANSPORT_TCP) ?
				xprt->address_strings[RPC_DISPLAY_PORT] : "0");
	xprt_put(xprt);
	return ret + 1;
}

static ssize_t rpc_sysfs_xprt_state_show(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 char *buf)
{
	struct rpc_xprt *xprt = rpc_sysfs_xprt_kobj_get_xprt(kobj);
	ssize_t ret;
	int locked, connected, connecting, close_wait, bound, binding,
	    closing, congested, cwnd_wait, write_space, offline, remove;

	if (!xprt)
		return 0;

	if (!xprt->state) {
		ret = sprintf(buf, "state=CLOSED\n");
	} else {
		locked = test_bit(XPRT_LOCKED, &xprt->state);
		connected = test_bit(XPRT_CONNECTED, &xprt->state);
		connecting = test_bit(XPRT_CONNECTING, &xprt->state);
		close_wait = test_bit(XPRT_CLOSE_WAIT, &xprt->state);
		bound = test_bit(XPRT_BOUND, &xprt->state);
		binding = test_bit(XPRT_BINDING, &xprt->state);
		closing = test_bit(XPRT_CLOSING, &xprt->state);
		congested = test_bit(XPRT_CONGESTED, &xprt->state);
		cwnd_wait = test_bit(XPRT_CWND_WAIT, &xprt->state);
		write_space = test_bit(XPRT_WRITE_SPACE, &xprt->state);
		offline = test_bit(XPRT_OFFLINE, &xprt->state);
		remove = test_bit(XPRT_REMOVE, &xprt->state);

		ret = sprintf(buf, "state=%s %s %s %s %s %s %s %s %s %s %s %s\n",
			      locked ? "LOCKED" : "",
			      connected ? "CONNECTED" : "",
			      connecting ? "CONNECTING" : "",
			      close_wait ? "CLOSE_WAIT" : "",
			      bound ? "BOUND" : "",
			      binding ? "BOUNDING" : "",
			      closing ? "CLOSING" : "",
			      congested ? "CONGESTED" : "",
			      cwnd_wait ? "CWND_WAIT" : "",
			      write_space ? "WRITE_SPACE" : "",
			      offline ? "OFFLINE" : "",
			      remove ? "REMOVE" : "");
	}

	xprt_put(xprt);
	return ret + 1;
}

static ssize_t rpc_sysfs_xprt_switch_info_show(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       char *buf)
{
	struct rpc_xprt_switch *xprt_switch =
		rpc_sysfs_xprt_switch_kobj_get_xprt(kobj);
	ssize_t ret;

	if (!xprt_switch)
		return 0;
	ret = sprintf(buf, "num_xprts=%u\nnum_active=%u\n"
		      "num_unique_destaddr=%u\nqueue_len=%ld\n",
		      xprt_switch->xps_nxprts, xprt_switch->xps_nactive,
		      xprt_switch->xps_nunique_destaddr_xprts,
		      atomic_long_read(&xprt_switch->xps_queuelen));
	xprt_switch_put(xprt_switch);
	return ret + 1;
}

static ssize_t rpc_sysfs_xprt_dstaddr_store(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    const char *buf, size_t count)
{
	struct rpc_xprt *xprt = rpc_sysfs_xprt_kobj_get_xprt(kobj);
	struct sockaddr *saddr;
	char *dst_addr;
	int port;
	struct xprt_addr *saved_addr;
	size_t buf_len;

	if (!xprt)
		return 0;
	if (!(xprt->xprt_class->ident == XPRT_TRANSPORT_TCP ||
	      xprt->xprt_class->ident == XPRT_TRANSPORT_RDMA)) {
		xprt_put(xprt);
		return -EOPNOTSUPP;
	}

	if (wait_on_bit_lock(&xprt->state, XPRT_LOCKED, TASK_KILLABLE)) {
		count = -EINTR;
		goto out_put;
	}
	saddr = (struct sockaddr *)&xprt->addr;
	port = rpc_get_port(saddr);

	/* buf_len is the len until the first occurence of either
	 * '\n' or '\0'
	 */
	buf_len = strcspn(buf, "\n");

	dst_addr = kstrndup(buf, buf_len, GFP_KERNEL);
	if (!dst_addr)
		goto out_err;
	saved_addr = kzalloc(sizeof(*saved_addr), GFP_KERNEL);
	if (!saved_addr)
		goto out_err_free;
	saved_addr->addr =
		rcu_dereference_raw(xprt->address_strings[RPC_DISPLAY_ADDR]);
	rcu_assign_pointer(xprt->address_strings[RPC_DISPLAY_ADDR], dst_addr);
	call_rcu(&saved_addr->rcu, free_xprt_addr);
	xprt->addrlen = rpc_pton(xprt->xprt_net, buf, buf_len, saddr,
				 sizeof(*saddr));
	rpc_set_port(saddr, port);

	xprt_force_disconnect(xprt);
out:
	xprt_release_write(xprt, NULL);
out_put:
	xprt_put(xprt);
	return count;
out_err_free:
	kfree(dst_addr);
out_err:
	count = -ENOMEM;
	goto out;
}

static ssize_t rpc_sysfs_xprt_state_change(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	struct rpc_xprt *xprt = rpc_sysfs_xprt_kobj_get_xprt(kobj);
	int offline = 0, online = 0, remove = 0;
	struct rpc_xprt_switch *xps = rpc_sysfs_xprt_kobj_get_xprt_switch(kobj);

	if (!xprt)
		return 0;

	if (!strncmp(buf, "offline", 7))
		offline = 1;
	else if (!strncmp(buf, "online", 6))
		online = 1;
	else if (!strncmp(buf, "remove", 6))
		remove = 1;
	else
		return -EINVAL;

	if (wait_on_bit_lock(&xprt->state, XPRT_LOCKED, TASK_KILLABLE)) {
		count = -EINTR;
		goto out_put;
	}
	if (xprt->main) {
		count = -EINVAL;
		goto release_tasks;
	}
	if (offline) {
		set_bit(XPRT_OFFLINE, &xprt->state);
		spin_lock(&xps->xps_lock);
		xps->xps_nactive--;
		spin_unlock(&xps->xps_lock);
	} else if (online) {
		clear_bit(XPRT_OFFLINE, &xprt->state);
		spin_lock(&xps->xps_lock);
		xps->xps_nactive++;
		spin_unlock(&xps->xps_lock);
	} else if (remove) {
		if (test_bit(XPRT_OFFLINE, &xprt->state)) {
			set_bit(XPRT_REMOVE, &xprt->state);
			xprt_force_disconnect(xprt);
			if (test_bit(XPRT_CONNECTED, &xprt->state)) {
				if (!xprt->sending.qlen &&
				    !xprt->pending.qlen &&
				    !xprt->backlog.qlen &&
				    !atomic_long_read(&xprt->queuelen))
					rpc_xprt_switch_remove_xprt(xps, xprt);
			}
		} else {
			count = -EINVAL;
		}
	}

release_tasks:
	xprt_release_write(xprt, NULL);
out_put:
	xprt_put(xprt);
	xprt_switch_put(xps);
	return count;
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

static void rpc_sysfs_xprt_release(struct kobject *kobj)
{
	struct rpc_sysfs_xprt *xprt;

	xprt = container_of(kobj, struct rpc_sysfs_xprt, kobject);
	kfree(xprt);
}

static const void *rpc_sysfs_client_namespace(struct kobject *kobj)
{
	return container_of(kobj, struct rpc_sysfs_client, kobject)->net;
}

static const void *rpc_sysfs_xprt_switch_namespace(struct kobject *kobj)
{
	return container_of(kobj, struct rpc_sysfs_xprt_switch, kobject)->net;
}

static const void *rpc_sysfs_xprt_namespace(struct kobject *kobj)
{
	return container_of(kobj, struct rpc_sysfs_xprt,
			    kobject)->xprt->xprt_net;
}

static struct kobj_attribute rpc_sysfs_xprt_dstaddr = __ATTR(dstaddr,
	0644, rpc_sysfs_xprt_dstaddr_show, rpc_sysfs_xprt_dstaddr_store);

static struct kobj_attribute rpc_sysfs_xprt_srcaddr = __ATTR(srcaddr,
	0644, rpc_sysfs_xprt_srcaddr_show, NULL);

static struct kobj_attribute rpc_sysfs_xprt_info = __ATTR(xprt_info,
	0444, rpc_sysfs_xprt_info_show, NULL);

static struct kobj_attribute rpc_sysfs_xprt_change_state = __ATTR(xprt_state,
	0644, rpc_sysfs_xprt_state_show, rpc_sysfs_xprt_state_change);

static struct attribute *rpc_sysfs_xprt_attrs[] = {
	&rpc_sysfs_xprt_dstaddr.attr,
	&rpc_sysfs_xprt_srcaddr.attr,
	&rpc_sysfs_xprt_info.attr,
	&rpc_sysfs_xprt_change_state.attr,
	NULL,
};

static struct kobj_attribute rpc_sysfs_xprt_switch_info =
	__ATTR(xprt_switch_info, 0444, rpc_sysfs_xprt_switch_info_show, NULL);

static struct attribute *rpc_sysfs_xprt_switch_attrs[] = {
	&rpc_sysfs_xprt_switch_info.attr,
	NULL,
};

static struct kobj_type rpc_sysfs_client_type = {
	.release = rpc_sysfs_client_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.namespace = rpc_sysfs_client_namespace,
};

static struct kobj_type rpc_sysfs_xprt_switch_type = {
	.release = rpc_sysfs_xprt_switch_release,
	.default_attrs = rpc_sysfs_xprt_switch_attrs,
	.sysfs_ops = &kobj_sysfs_ops,
	.namespace = rpc_sysfs_xprt_switch_namespace,
};

static struct kobj_type rpc_sysfs_xprt_type = {
	.release = rpc_sysfs_xprt_release,
	.default_attrs = rpc_sysfs_xprt_attrs,
	.sysfs_ops = &kobj_sysfs_ops,
	.namespace = rpc_sysfs_xprt_namespace,
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

static struct rpc_sysfs_xprt *rpc_sysfs_xprt_alloc(struct kobject *parent,
						   struct rpc_xprt *xprt,
						   gfp_t gfp_flags)
{
	struct rpc_sysfs_xprt *p;

	p = kzalloc(sizeof(*p), gfp_flags);
	if (!p)
		goto out;
	p->kobject.kset = rpc_sunrpc_kset;
	if (kobject_init_and_add(&p->kobject, &rpc_sysfs_xprt_type,
				 parent, "xprt-%d-%s", xprt->id,
				 xprt->address_strings[RPC_DISPLAY_PROTO]) == 0)
		return p;
	kobject_put(&p->kobject);
out:
	return NULL;
}

void rpc_sysfs_client_setup(struct rpc_clnt *clnt,
			    struct rpc_xprt_switch *xprt_switch,
			    struct net *net)
{
	struct rpc_sysfs_client *rpc_client;

	rpc_client = rpc_sysfs_client_alloc(rpc_sunrpc_client_kobj,
					    net, clnt->cl_clid);
	if (rpc_client) {
		char name[] = "switch";
		struct rpc_sysfs_xprt_switch *xswitch =
			(struct rpc_sysfs_xprt_switch *)xprt_switch->xps_sysfs;
		int ret;

		clnt->cl_sysfs = rpc_client;
		rpc_client->clnt = clnt;
		rpc_client->xprt_switch = xprt_switch;
		kobject_uevent(&rpc_client->kobject, KOBJ_ADD);
		ret = sysfs_create_link_nowarn(&rpc_client->kobject,
					       &xswitch->kobject, name);
		if (ret)
			pr_warn("can't create link to %s in sysfs (%d)\n",
				name, ret);
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

void rpc_sysfs_xprt_setup(struct rpc_xprt_switch *xprt_switch,
			  struct rpc_xprt *xprt,
			  gfp_t gfp_flags)
{
	struct rpc_sysfs_xprt *rpc_xprt;
	struct rpc_sysfs_xprt_switch *switch_obj =
		(struct rpc_sysfs_xprt_switch *)xprt_switch->xps_sysfs;

	rpc_xprt = rpc_sysfs_xprt_alloc(&switch_obj->kobject, xprt, gfp_flags);
	if (rpc_xprt) {
		xprt->xprt_sysfs = rpc_xprt;
		rpc_xprt->xprt = xprt;
		rpc_xprt->xprt_switch = xprt_switch;
		kobject_uevent(&rpc_xprt->kobject, KOBJ_ADD);
	}
}

void rpc_sysfs_client_destroy(struct rpc_clnt *clnt)
{
	struct rpc_sysfs_client *rpc_client = clnt->cl_sysfs;

	if (rpc_client) {
		char name[] = "switch";

		sysfs_remove_link(&rpc_client->kobject, name);
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

void rpc_sysfs_xprt_destroy(struct rpc_xprt *xprt)
{
	struct rpc_sysfs_xprt *rpc_xprt = xprt->xprt_sysfs;

	if (rpc_xprt) {
		kobject_uevent(&rpc_xprt->kobject, KOBJ_REMOVE);
		kobject_del(&rpc_xprt->kobject);
		kobject_put(&rpc_xprt->kobject);
		xprt->xprt_sysfs = NULL;
	}
}
