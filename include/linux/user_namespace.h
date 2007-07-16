#ifndef _LINUX_USER_NAMESPACE_H
#define _LINUX_USER_NAMESPACE_H

#include <linux/kref.h>
#include <linux/nsproxy.h>
#include <linux/sched.h>

#define UIDHASH_BITS	(CONFIG_BASE_SMALL ? 3 : 8)
#define UIDHASH_SZ	(1 << UIDHASH_BITS)

struct user_namespace {
	struct kref		kref;
	struct list_head	uidhash_table[UIDHASH_SZ];
	struct user_struct	*root_user;
};

extern struct user_namespace init_user_ns;

#ifdef CONFIG_USER_NS

static inline struct user_namespace *get_user_ns(struct user_namespace *ns)
{
	if (ns)
		kref_get(&ns->kref);
	return ns;
}

extern struct user_namespace *copy_user_ns(int flags,
					   struct user_namespace *old_ns);
extern void free_user_ns(struct kref *kref);

static inline void put_user_ns(struct user_namespace *ns)
{
	if (ns)
		kref_put(&ns->kref, free_user_ns);
}

#else

static inline struct user_namespace *get_user_ns(struct user_namespace *ns)
{
	return &init_user_ns;
}

static inline struct user_namespace *copy_user_ns(int flags,
						  struct user_namespace *old_ns)
{
	return NULL;
}

static inline void put_user_ns(struct user_namespace *ns)
{
}

#endif

#endif /* _LINUX_USER_H */
