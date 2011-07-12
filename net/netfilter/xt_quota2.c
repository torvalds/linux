/*
 * xt_quota2 - enhanced xt_quota that can count upwards and in packets
 * as a minimal accounting match.
 * by Jan Engelhardt <jengelh@medozas.de>, 2008
 *
 * Originally based on xt_quota.c:
 * 	netfilter module to enforce network quotas
 * 	Sam Johnston <samj@samj.net>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License; either
 *	version 2 of the License, as published by the Free Software Foundation.
 */
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_quota2.h>

/**
 * @lock:	lock to protect quota writers from each other
 */
struct xt_quota_counter {
	u_int64_t quota;
	spinlock_t lock;
	struct list_head list;
	atomic_t ref;
	char name[sizeof(((struct xt_quota_mtinfo2 *)NULL)->name)];
	struct proc_dir_entry *procfs_entry;
};

static LIST_HEAD(counter_list);
static DEFINE_SPINLOCK(counter_list_lock);

static struct proc_dir_entry *proc_xt_quota;
static unsigned int quota_list_perms = S_IRUGO | S_IWUSR;
static unsigned int quota_list_uid   = 0;
static unsigned int quota_list_gid   = 0;
module_param_named(perms, quota_list_perms, uint, S_IRUGO | S_IWUSR);
module_param_named(uid, quota_list_uid, uint, S_IRUGO | S_IWUSR);
module_param_named(gid, quota_list_gid, uint, S_IRUGO | S_IWUSR);

static int quota_proc_read(char *page, char **start, off_t offset,
                           int count, int *eof, void *data)
{
	struct xt_quota_counter *e = data;
	int ret;

	spin_lock_bh(&e->lock);
	ret = snprintf(page, PAGE_SIZE, "%llu\n", e->quota);
	spin_unlock_bh(&e->lock);
	return ret;
}

static int quota_proc_write(struct file *file, const char __user *input,
                            unsigned long size, void *data)
{
	struct xt_quota_counter *e = data;
	char buf[sizeof("18446744073709551616")];

	if (size > sizeof(buf))
		size = sizeof(buf);
	if (copy_from_user(buf, input, size) != 0)
		return -EFAULT;
	buf[sizeof(buf)-1] = '\0';

	spin_lock_bh(&e->lock);
	e->quota = simple_strtoull(buf, NULL, 0);
	spin_unlock_bh(&e->lock);
	return size;
}

static struct xt_quota_counter *
q2_new_counter(const struct xt_quota_mtinfo2 *q, bool anon)
{
	struct xt_quota_counter *e;
	unsigned int size;

	/* Do not need all the procfs things for anonymous counters. */
	size = anon ? offsetof(typeof(*e), list) : sizeof(*e);
	e = kmalloc(size, GFP_KERNEL);
	if (e == NULL)
		return NULL;

	e->quota = q->quota;
	spin_lock_init(&e->lock);
	if (!anon) {
		INIT_LIST_HEAD(&e->list);
		atomic_set(&e->ref, 1);
		strlcpy(e->name, q->name, sizeof(e->name));
	}
	return e;
}

/**
 * q2_get_counter - get ref to counter or create new
 * @name:	name of counter
 */
static struct xt_quota_counter *
q2_get_counter(const struct xt_quota_mtinfo2 *q)
{
	struct proc_dir_entry *p;
	struct xt_quota_counter *e = NULL;
	struct xt_quota_counter *new_e;

	if (*q->name == '\0')
		return q2_new_counter(q, true);

	/* No need to hold a lock while getting a new counter */
	new_e = q2_new_counter(q, false);
	if (new_e == NULL)
		goto out;

	spin_lock_bh(&counter_list_lock);
	list_for_each_entry(e, &counter_list, list)
		if (strcmp(e->name, q->name) == 0) {
			atomic_inc(&e->ref);
			spin_unlock_bh(&counter_list_lock);
			kfree(new_e);
			pr_debug("xt_quota2: old counter name=%s", e->name);
			return e;
		}
	e = new_e;
	pr_debug("xt_quota2: new_counter name=%s", e->name);
	list_add_tail(&e->list, &counter_list);
	/* The entry having a refcount of 1 is not directly destructible.
	 * This func has not yet returned the new entry, thus iptables
	 * has not references for destroying this entry.
	 * For another rule to try to destroy it, it would 1st need for this
	 * func* to be re-invoked, acquire a new ref for the same named quota.
	 * Nobody will access the e->procfs_entry either.
	 * So release the lock. */
	spin_unlock_bh(&counter_list_lock);

	/* create_proc_entry() is not spin_lock happy */
	p = e->procfs_entry = create_proc_entry(e->name, quota_list_perms,
	                      proc_xt_quota);

	if (IS_ERR_OR_NULL(p)) {
		spin_lock_bh(&counter_list_lock);
		list_del(&e->list);
		spin_unlock_bh(&counter_list_lock);
		goto out;
	}
	p->data         = e;
	p->read_proc    = quota_proc_read;
	p->write_proc   = quota_proc_write;
	p->uid          = quota_list_uid;
	p->gid          = quota_list_gid;
	return e;

 out:
	kfree(e);
	return NULL;
}

static int quota_mt2_check(const struct xt_mtchk_param *par)
{
	struct xt_quota_mtinfo2 *q = par->matchinfo;

	pr_debug("xt_quota2: check() flags=0x%04x", q->flags);

	if (q->flags & ~XT_QUOTA_MASK)
		return -EINVAL;

	q->name[sizeof(q->name)-1] = '\0';
	if (*q->name == '.' || strchr(q->name, '/') != NULL) {
		printk(KERN_ERR "xt_quota.3: illegal name\n");
		return -EINVAL;
	}

	q->master = q2_get_counter(q);
	if (q->master == NULL) {
		printk(KERN_ERR "xt_quota.3: memory alloc failure\n");
		return -ENOMEM;
	}

	return 0;
}

static void quota_mt2_destroy(const struct xt_mtdtor_param *par)
{
	struct xt_quota_mtinfo2 *q = par->matchinfo;
	struct xt_quota_counter *e = q->master;

	if (*q->name == '\0') {
		kfree(e);
		return;
	}

	spin_lock_bh(&counter_list_lock);
	if (!atomic_dec_and_test(&e->ref)) {
		spin_unlock_bh(&counter_list_lock);
		return;
	}

	list_del(&e->list);
	remove_proc_entry(e->name, proc_xt_quota);
	spin_unlock_bh(&counter_list_lock);
	kfree(e);
}

static bool
quota_mt2(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct xt_quota_mtinfo2 *q = (void *)par->matchinfo;
	struct xt_quota_counter *e = q->master;
	bool ret = q->flags & XT_QUOTA_INVERT;

	spin_lock_bh(&e->lock);
	if (q->flags & XT_QUOTA_GROW) {
		/*
		 * While no_change is pointless in "grow" mode, we will
		 * implement it here simply to have a consistent behavior.
		 */
		if (!(q->flags & XT_QUOTA_NO_CHANGE)) {
			e->quota += (q->flags & XT_QUOTA_PACKET) ? 1 : skb->len;
		}
		ret = true;
	} else {
		if (e->quota >= skb->len) {
			if (!(q->flags & XT_QUOTA_NO_CHANGE))
				e->quota -= (q->flags & XT_QUOTA_PACKET) ? 1 : skb->len;
			ret = !ret;
		} else {
			/* we do not allow even small packets from now on */
			e->quota = 0;
		}
	}
	spin_unlock_bh(&e->lock);
	return ret;
}

static struct xt_match quota_mt2_reg[] __read_mostly = {
	{
		.name       = "quota2",
		.revision   = 3,
		.family     = NFPROTO_IPV4,
		.checkentry = quota_mt2_check,
		.match      = quota_mt2,
		.destroy    = quota_mt2_destroy,
		.matchsize  = sizeof(struct xt_quota_mtinfo2),
		.me         = THIS_MODULE,
	},
	{
		.name       = "quota2",
		.revision   = 3,
		.family     = NFPROTO_IPV6,
		.checkentry = quota_mt2_check,
		.match      = quota_mt2,
		.destroy    = quota_mt2_destroy,
		.matchsize  = sizeof(struct xt_quota_mtinfo2),
		.me         = THIS_MODULE,
	},
};

static int __init quota_mt2_init(void)
{
	int ret;
	pr_debug("xt_quota2: init()");

	proc_xt_quota = proc_mkdir("xt_quota", init_net.proc_net);
	if (proc_xt_quota == NULL)
		return -EACCES;

	ret = xt_register_matches(quota_mt2_reg, ARRAY_SIZE(quota_mt2_reg));
	if (ret < 0)
		remove_proc_entry("xt_quota", init_net.proc_net);
	pr_debug("xt_quota2: init() %d", ret);
	return ret;
}

static void __exit quota_mt2_exit(void)
{
	xt_unregister_matches(quota_mt2_reg, ARRAY_SIZE(quota_mt2_reg));
	remove_proc_entry("xt_quota", init_net.proc_net);
}

module_init(quota_mt2_init);
module_exit(quota_mt2_exit);
MODULE_DESCRIPTION("Xtables: countdown quota match; up counter");
MODULE_AUTHOR("Sam Johnston <samj@samj.net>");
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_quota2");
MODULE_ALIAS("ip6t_quota2");
