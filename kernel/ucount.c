// SPDX-License-Identifier: GPL-2.0-only

#include <linux/stat.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/hash.h>
#include <linux/kmemleak.h>
#include <linux/user_namespace.h>

#define UCOUNTS_HASHTABLE_BITS 10
static struct hlist_head ucounts_hashtable[(1 << UCOUNTS_HASHTABLE_BITS)];
static DEFINE_SPINLOCK(ucounts_lock);

#define ucounts_hashfn(ns, uid)						\
	hash_long((unsigned long)__kuid_val(uid) + (unsigned long)(ns), \
		  UCOUNTS_HASHTABLE_BITS)
#define ucounts_hashentry(ns, uid)	\
	(ucounts_hashtable + ucounts_hashfn(ns, uid))


#ifdef CONFIG_SYSCTL
static struct ctl_table_set *
set_lookup(struct ctl_table_root *root)
{
	return &current_user_ns()->set;
}

static int set_is_seen(struct ctl_table_set *set)
{
	return &current_user_ns()->set == set;
}

static int set_permissions(struct ctl_table_header *head,
				  struct ctl_table *table)
{
	struct user_namespace *user_ns =
		container_of(head->set, struct user_namespace, set);
	int mode;

	/* Allow users with CAP_SYS_RESOURCE unrestrained access */
	if (ns_capable(user_ns, CAP_SYS_RESOURCE))
		mode = (table->mode & S_IRWXU) >> 6;
	else
	/* Allow all others at most read-only access */
		mode = table->mode & S_IROTH;
	return (mode << 6) | (mode << 3) | mode;
}

static struct ctl_table_root set_root = {
	.lookup = set_lookup,
	.permissions = set_permissions,
};

#define UCOUNT_ENTRY(name)				\
	{						\
		.procname	= name,			\
		.maxlen		= sizeof(int),		\
		.mode		= 0644,			\
		.proc_handler	= proc_dointvec_minmax,	\
		.extra1		= SYSCTL_ZERO,		\
		.extra2		= SYSCTL_INT_MAX,	\
	}
static struct ctl_table user_table[] = {
	UCOUNT_ENTRY("max_user_namespaces"),
	UCOUNT_ENTRY("max_pid_namespaces"),
	UCOUNT_ENTRY("max_uts_namespaces"),
	UCOUNT_ENTRY("max_ipc_namespaces"),
	UCOUNT_ENTRY("max_net_namespaces"),
	UCOUNT_ENTRY("max_mnt_namespaces"),
	UCOUNT_ENTRY("max_cgroup_namespaces"),
	UCOUNT_ENTRY("max_time_namespaces"),
#ifdef CONFIG_INOTIFY_USER
	UCOUNT_ENTRY("max_inotify_instances"),
	UCOUNT_ENTRY("max_inotify_watches"),
#endif
#ifdef CONFIG_FANOTIFY
	UCOUNT_ENTRY("max_fanotify_groups"),
	UCOUNT_ENTRY("max_fanotify_marks"),
#endif
	{ }
};
#endif /* CONFIG_SYSCTL */

bool setup_userns_sysctls(struct user_namespace *ns)
{
#ifdef CONFIG_SYSCTL
	struct ctl_table *tbl;

	BUILD_BUG_ON(ARRAY_SIZE(user_table) != UCOUNT_COUNTS + 1);
	setup_sysctl_set(&ns->set, &set_root, set_is_seen);
	tbl = kmemdup(user_table, sizeof(user_table), GFP_KERNEL);
	if (tbl) {
		int i;
		for (i = 0; i < UCOUNT_COUNTS; i++) {
			tbl[i].data = &ns->ucount_max[i];
		}
		ns->sysctls = __register_sysctl_table(&ns->set, "user", tbl);
	}
	if (!ns->sysctls) {
		kfree(tbl);
		retire_sysctl_set(&ns->set);
		return false;
	}
#endif
	return true;
}

void retire_userns_sysctls(struct user_namespace *ns)
{
#ifdef CONFIG_SYSCTL
	struct ctl_table *tbl;

	tbl = ns->sysctls->ctl_table_arg;
	unregister_sysctl_table(ns->sysctls);
	retire_sysctl_set(&ns->set);
	kfree(tbl);
#endif
}

static struct ucounts *find_ucounts(struct user_namespace *ns, kuid_t uid, struct hlist_head *hashent)
{
	struct ucounts *ucounts;

	hlist_for_each_entry(ucounts, hashent, node) {
		if (uid_eq(ucounts->uid, uid) && (ucounts->ns == ns))
			return ucounts;
	}
	return NULL;
}

static struct ucounts *get_ucounts(struct user_namespace *ns, kuid_t uid)
{
	struct hlist_head *hashent = ucounts_hashentry(ns, uid);
	struct ucounts *ucounts, *new;

	spin_lock_irq(&ucounts_lock);
	ucounts = find_ucounts(ns, uid, hashent);
	if (!ucounts) {
		spin_unlock_irq(&ucounts_lock);

		new = kzalloc(sizeof(*new), GFP_KERNEL);
		if (!new)
			return NULL;

		new->ns = ns;
		new->uid = uid;
		new->count = 0;

		spin_lock_irq(&ucounts_lock);
		ucounts = find_ucounts(ns, uid, hashent);
		if (ucounts) {
			kfree(new);
		} else {
			hlist_add_head(&new->node, hashent);
			ucounts = new;
		}
	}
	if (ucounts->count == INT_MAX)
		ucounts = NULL;
	else
		ucounts->count += 1;
	spin_unlock_irq(&ucounts_lock);
	return ucounts;
}

static void put_ucounts(struct ucounts *ucounts)
{
	unsigned long flags;

	spin_lock_irqsave(&ucounts_lock, flags);
	ucounts->count -= 1;
	if (!ucounts->count)
		hlist_del_init(&ucounts->node);
	else
		ucounts = NULL;
	spin_unlock_irqrestore(&ucounts_lock, flags);

	kfree(ucounts);
}

static inline bool atomic_inc_below(atomic_t *v, int u)
{
	int c, old;
	c = atomic_read(v);
	for (;;) {
		if (unlikely(c >= u))
			return false;
		old = atomic_cmpxchg(v, c, c+1);
		if (likely(old == c))
			return true;
		c = old;
	}
}

struct ucounts *inc_ucount(struct user_namespace *ns, kuid_t uid,
			   enum ucount_type type)
{
	struct ucounts *ucounts, *iter, *bad;
	struct user_namespace *tns;
	ucounts = get_ucounts(ns, uid);
	for (iter = ucounts; iter; iter = tns->ucounts) {
		int max;
		tns = iter->ns;
		max = READ_ONCE(tns->ucount_max[type]);
		if (!atomic_inc_below(&iter->ucount[type], max))
			goto fail;
	}
	return ucounts;
fail:
	bad = iter;
	for (iter = ucounts; iter != bad; iter = iter->ns->ucounts)
		atomic_dec(&iter->ucount[type]);

	put_ucounts(ucounts);
	return NULL;
}

void dec_ucount(struct ucounts *ucounts, enum ucount_type type)
{
	struct ucounts *iter;
	for (iter = ucounts; iter; iter = iter->ns->ucounts) {
		int dec = atomic_dec_if_positive(&iter->ucount[type]);
		WARN_ON_ONCE(dec < 0);
	}
	put_ucounts(ucounts);
}

static __init int user_namespace_sysctl_init(void)
{
#ifdef CONFIG_SYSCTL
	static struct ctl_table_header *user_header;
	static struct ctl_table empty[1];
	/*
	 * It is necessary to register the user directory in the
	 * default set so that registrations in the child sets work
	 * properly.
	 */
	user_header = register_sysctl("user", empty);
	kmemleak_ignore(user_header);
	BUG_ON(!user_header);
	BUG_ON(!setup_userns_sysctls(&init_user_ns));
#endif
	return 0;
}
subsys_initcall(user_namespace_sysctl_init);
