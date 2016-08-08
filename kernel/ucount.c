/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2 of the
 *  License.
 */

#include <linux/stat.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>

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

static int zero = 0;
static int int_max = INT_MAX;
static struct ctl_table userns_table[] = {
	{
		.procname	= "max_user_namespaces",
		.data		= &init_user_ns.max_user_namespaces,
		.maxlen		= sizeof(init_user_ns.max_user_namespaces),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &int_max,
	},
	{ }
};
#endif /* CONFIG_SYSCTL */

bool setup_userns_sysctls(struct user_namespace *ns)
{
#ifdef CONFIG_SYSCTL
	struct ctl_table *tbl;
	setup_sysctl_set(&ns->set, &set_root, set_is_seen);
	tbl = kmemdup(userns_table, sizeof(userns_table), GFP_KERNEL);
	if (tbl) {
		tbl[0].data = &ns->max_user_namespaces;

		ns->sysctls = __register_sysctl_table(&ns->set, "userns", tbl);
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

bool inc_user_namespaces(struct user_namespace *ns)
{
	struct user_namespace *pos, *bad;
	for (pos = ns; pos; pos = pos->parent) {
		int max = READ_ONCE(pos->max_user_namespaces);
		if (!atomic_inc_below(&pos->user_namespaces, max))
			goto fail;
	}
	return true;
fail:
	bad = pos;
	for (pos = ns; pos != bad; pos = pos->parent)
		atomic_dec(&pos->user_namespaces);

	return false;
}

void dec_user_namespaces(struct user_namespace *ns)
{
	struct user_namespace *pos;
	for (pos = ns; pos; pos = pos->parent) {
		int dec = atomic_dec_if_positive(&pos->user_namespaces);
		WARN_ON_ONCE(dec < 0);
	}
}

static __init int user_namespace_sysctl_init(void)
{
#ifdef CONFIG_SYSCTL
	static struct ctl_table_header *userns_header;
	static struct ctl_table empty[1];
	/*
	 * It is necessary to register the userns directory in the
	 * default set so that registrations in the child sets work
	 * properly.
	 */
	userns_header = register_sysctl("userns", empty);
	BUG_ON(!userns_header);
	BUG_ON(!setup_userns_sysctls(&init_user_ns));
#endif
	return 0;
}
subsys_initcall(user_namespace_sysctl_init);


