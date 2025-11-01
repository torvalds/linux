/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UTS_NAMESPACE_H
#define _LINUX_UTS_NAMESPACE_H

#include <linux/ns_common.h>
#include <uapi/linux/utsname.h>

struct user_namespace;
extern struct user_namespace init_user_ns;

struct uts_namespace {
	struct new_utsname name;
	struct user_namespace *user_ns;
	struct ucounts *ucounts;
	struct ns_common ns;
} __randomize_layout;

extern struct uts_namespace init_uts_ns;

#ifdef CONFIG_UTS_NS
static inline struct uts_namespace *to_uts_ns(struct ns_common *ns)
{
	return container_of(ns, struct uts_namespace, ns);
}

static inline void get_uts_ns(struct uts_namespace *ns)
{
	ns_ref_inc(ns);
}

extern struct uts_namespace *copy_utsname(u64 flags,
	struct user_namespace *user_ns, struct uts_namespace *old_ns);
extern void free_uts_ns(struct uts_namespace *ns);

static inline void put_uts_ns(struct uts_namespace *ns)
{
	if (ns_ref_put(ns))
		free_uts_ns(ns);
}

void uts_ns_init(void);
#else
static inline void get_uts_ns(struct uts_namespace *ns)
{
}

static inline void put_uts_ns(struct uts_namespace *ns)
{
}

static inline struct uts_namespace *copy_utsname(u64 flags,
	struct user_namespace *user_ns, struct uts_namespace *old_ns)
{
	if (flags & CLONE_NEWUTS)
		return ERR_PTR(-EINVAL);

	return old_ns;
}

static inline void uts_ns_init(void)
{
}
#endif

#endif /* _LINUX_UTS_NAMESPACE_H */
