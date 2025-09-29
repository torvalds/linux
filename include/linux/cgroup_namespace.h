/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CGROUP_NAMESPACE_H
#define _LINUX_CGROUP_NAMESPACE_H

#include <linux/ns_common.h>

struct cgroup_namespace {
	struct ns_common	ns;
	struct user_namespace	*user_ns;
	struct ucounts		*ucounts;
	struct css_set          *root_cset;
};

extern struct cgroup_namespace init_cgroup_ns;

#ifdef CONFIG_CGROUPS

static inline struct cgroup_namespace *to_cg_ns(struct ns_common *ns)
{
	return container_of(ns, struct cgroup_namespace, ns);
}

void free_cgroup_ns(struct cgroup_namespace *ns);

struct cgroup_namespace *copy_cgroup_ns(u64 flags,
					struct user_namespace *user_ns,
					struct cgroup_namespace *old_ns);

int cgroup_path_ns(struct cgroup *cgrp, char *buf, size_t buflen,
		   struct cgroup_namespace *ns);

static inline void get_cgroup_ns(struct cgroup_namespace *ns)
{
	ns_ref_inc(ns);
}

static inline void put_cgroup_ns(struct cgroup_namespace *ns)
{
	if (ns_ref_put(ns))
		free_cgroup_ns(ns);
}

#else /* !CONFIG_CGROUPS */

static inline void free_cgroup_ns(struct cgroup_namespace *ns) { }
static inline struct cgroup_namespace *
copy_cgroup_ns(u64 flags, struct user_namespace *user_ns,
	       struct cgroup_namespace *old_ns)
{
	return old_ns;
}

static inline void get_cgroup_ns(struct cgroup_namespace *ns) { }
static inline void put_cgroup_ns(struct cgroup_namespace *ns) { }

#endif /* !CONFIG_CGROUPS */

#endif /* _LINUX_CGROUP_NAMESPACE_H */
