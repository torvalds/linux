/*
 * net/core/netclassid_cgroup.c	Classid Cgroupfs Handling
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 */

#include <linux/slab.h>
#include <linux/cgroup.h>
#include <linux/fdtable.h>
#include <linux/sched/task.h>

#include <net/cls_cgroup.h>
#include <net/sock.h>

static inline struct cgroup_cls_state *css_cls_state(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct cgroup_cls_state, css) : NULL;
}

struct cgroup_cls_state *task_cls_state(struct task_struct *p)
{
	return css_cls_state(task_css_check(p, net_cls_cgrp_id,
					    rcu_read_lock_bh_held()));
}
EXPORT_SYMBOL_GPL(task_cls_state);

static struct cgroup_subsys_state *
cgrp_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct cgroup_cls_state *cs;

	cs = kzalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return ERR_PTR(-ENOMEM);

	return &cs->css;
}

static int cgrp_css_online(struct cgroup_subsys_state *css)
{
	struct cgroup_cls_state *cs = css_cls_state(css);
	struct cgroup_cls_state *parent = css_cls_state(css->parent);

	if (parent)
		cs->classid = parent->classid;

	return 0;
}

static void cgrp_css_free(struct cgroup_subsys_state *css)
{
	kfree(css_cls_state(css));
}

/*
 * To avoid freezing of sockets creation for tasks with big number of threads
 * and opened sockets lets release file_lock every 1000 iterated descriptors.
 * New sockets will already have been created with new classid.
 */

struct update_classid_context {
	u32 classid;
	unsigned int batch;
};

#define UPDATE_CLASSID_BATCH 1000

static int update_classid_sock(const void *v, struct file *file, unsigned n)
{
	int err;
	struct update_classid_context *ctx = (void *)v;
	struct socket *sock = sock_from_file(file, &err);

	if (sock) {
		spin_lock(&cgroup_sk_update_lock);
		sock_cgroup_set_classid(&sock->sk->sk_cgrp_data, ctx->classid);
		spin_unlock(&cgroup_sk_update_lock);
	}
	if (--ctx->batch == 0) {
		ctx->batch = UPDATE_CLASSID_BATCH;
		return n + 1;
	}
	return 0;
}

static void update_classid_task(struct task_struct *p, u32 classid)
{
	struct update_classid_context ctx = {
		.classid = classid,
		.batch = UPDATE_CLASSID_BATCH
	};
	unsigned int fd = 0;

	do {
		task_lock(p);
		fd = iterate_fd(p->files, fd, update_classid_sock, &ctx);
		task_unlock(p);
		cond_resched();
	} while (fd);
}

static void cgrp_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;
	struct task_struct *p;

	cgroup_taskset_for_each(p, css, tset) {
		update_classid_task(p, css_cls_state(css)->classid);
	}
}

static u64 read_classid(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return css_cls_state(css)->classid;
}

static int write_classid(struct cgroup_subsys_state *css, struct cftype *cft,
			 u64 value)
{
	struct cgroup_cls_state *cs = css_cls_state(css);
	struct css_task_iter it;
	struct task_struct *p;

	cgroup_sk_alloc_disable();

	cs->classid = (u32)value;

	css_task_iter_start(css, 0, &it);
	while ((p = css_task_iter_next(&it))) {
		update_classid_task(p, cs->classid);
		cond_resched();
	}
	css_task_iter_end(&it);

	return 0;
}

static struct cftype ss_files[] = {
	{
		.name		= "classid",
		.read_u64	= read_classid,
		.write_u64	= write_classid,
	},
	{ }	/* terminate */
};

struct cgroup_subsys net_cls_cgrp_subsys = {
	.css_alloc		= cgrp_css_alloc,
	.css_online		= cgrp_css_online,
	.css_free		= cgrp_css_free,
	.attach			= cgrp_attach,
	.legacy_cftypes		= ss_files,
};
