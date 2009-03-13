/*
 * The "user cache".
 *
 * (C) Copyright 1991-2000 Linus Torvalds
 *
 * We have a per-user structure to keep track of how many
 * processes, files etc the user has claimed, in order to be
 * able to have per-user limits for system resources. 
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/key.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/user_namespace.h>
#include "cred-internals.h"

struct user_namespace init_user_ns = {
	.kref = {
		.refcount	= ATOMIC_INIT(1),
	},
	.creator = &root_user,
};
EXPORT_SYMBOL_GPL(init_user_ns);

/*
 * UID task count cache, to get fast user lookup in "alloc_uid"
 * when changing user ID's (ie setuid() and friends).
 */

#define UIDHASH_MASK		(UIDHASH_SZ - 1)
#define __uidhashfn(uid)	(((uid >> UIDHASH_BITS) + uid) & UIDHASH_MASK)
#define uidhashentry(ns, uid)	((ns)->uidhash_table + __uidhashfn((uid)))

static struct kmem_cache *uid_cachep;

/*
 * The uidhash_lock is mostly taken from process context, but it is
 * occasionally also taken from softirq/tasklet context, when
 * task-structs get RCU-freed. Hence all locking must be softirq-safe.
 * But free_uid() is also called with local interrupts disabled, and running
 * local_bh_enable() with local interrupts disabled is an error - we'll run
 * softirq callbacks, and they can unconditionally enable interrupts, and
 * the caller of free_uid() didn't expect that..
 */
static DEFINE_SPINLOCK(uidhash_lock);

/* root_user.__count is 2, 1 for init task cred, 1 for init_user_ns->creator */
struct user_struct root_user = {
	.__count	= ATOMIC_INIT(2),
	.processes	= ATOMIC_INIT(1),
	.files		= ATOMIC_INIT(0),
	.sigpending	= ATOMIC_INIT(0),
	.locked_shm     = 0,
	.user_ns	= &init_user_ns,
#ifdef CONFIG_USER_SCHED
	.tg		= &init_task_group,
#endif
};

/*
 * These routines must be called with the uidhash spinlock held!
 */
static void uid_hash_insert(struct user_struct *up, struct hlist_head *hashent)
{
	hlist_add_head(&up->uidhash_node, hashent);
}

static void uid_hash_remove(struct user_struct *up)
{
	hlist_del_init(&up->uidhash_node);
	put_user_ns(up->user_ns);
}

static struct user_struct *uid_hash_find(uid_t uid, struct hlist_head *hashent)
{
	struct user_struct *user;
	struct hlist_node *h;

	hlist_for_each_entry(user, h, hashent, uidhash_node) {
		if (user->uid == uid) {
			atomic_inc(&user->__count);
			return user;
		}
	}

	return NULL;
}

#ifdef CONFIG_USER_SCHED

static void sched_destroy_user(struct user_struct *up)
{
	sched_destroy_group(up->tg);
}

static int sched_create_user(struct user_struct *up)
{
	int rc = 0;

	up->tg = sched_create_group(&root_task_group);
	if (IS_ERR(up->tg))
		rc = -ENOMEM;

	set_tg_uid(up);

	return rc;
}

#else	/* CONFIG_USER_SCHED */

static void sched_destroy_user(struct user_struct *up) { }
static int sched_create_user(struct user_struct *up) { return 0; }

#endif	/* CONFIG_USER_SCHED */

#if defined(CONFIG_USER_SCHED) && defined(CONFIG_SYSFS)

static struct kset *uids_kset; /* represents the /sys/kernel/uids/ directory */
static DEFINE_MUTEX(uids_mutex);

static inline void uids_mutex_lock(void)
{
	mutex_lock(&uids_mutex);
}

static inline void uids_mutex_unlock(void)
{
	mutex_unlock(&uids_mutex);
}

/* uid directory attributes */
#ifdef CONFIG_FAIR_GROUP_SCHED
static ssize_t cpu_shares_show(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       char *buf)
{
	struct user_struct *up = container_of(kobj, struct user_struct, kobj);

	return sprintf(buf, "%lu\n", sched_group_shares(up->tg));
}

static ssize_t cpu_shares_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t size)
{
	struct user_struct *up = container_of(kobj, struct user_struct, kobj);
	unsigned long shares;
	int rc;

	sscanf(buf, "%lu", &shares);

	rc = sched_group_set_shares(up->tg, shares);

	return (rc ? rc : size);
}

static struct kobj_attribute cpu_share_attr =
	__ATTR(cpu_share, 0644, cpu_shares_show, cpu_shares_store);
#endif

#ifdef CONFIG_RT_GROUP_SCHED
static ssize_t cpu_rt_runtime_show(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   char *buf)
{
	struct user_struct *up = container_of(kobj, struct user_struct, kobj);

	return sprintf(buf, "%ld\n", sched_group_rt_runtime(up->tg));
}

static ssize_t cpu_rt_runtime_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t size)
{
	struct user_struct *up = container_of(kobj, struct user_struct, kobj);
	unsigned long rt_runtime;
	int rc;

	sscanf(buf, "%ld", &rt_runtime);

	rc = sched_group_set_rt_runtime(up->tg, rt_runtime);

	return (rc ? rc : size);
}

static struct kobj_attribute cpu_rt_runtime_attr =
	__ATTR(cpu_rt_runtime, 0644, cpu_rt_runtime_show, cpu_rt_runtime_store);

static ssize_t cpu_rt_period_show(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   char *buf)
{
	struct user_struct *up = container_of(kobj, struct user_struct, kobj);

	return sprintf(buf, "%lu\n", sched_group_rt_period(up->tg));
}

static ssize_t cpu_rt_period_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t size)
{
	struct user_struct *up = container_of(kobj, struct user_struct, kobj);
	unsigned long rt_period;
	int rc;

	sscanf(buf, "%lu", &rt_period);

	rc = sched_group_set_rt_period(up->tg, rt_period);

	return (rc ? rc : size);
}

static struct kobj_attribute cpu_rt_period_attr =
	__ATTR(cpu_rt_period, 0644, cpu_rt_period_show, cpu_rt_period_store);
#endif

/* default attributes per uid directory */
static struct attribute *uids_attributes[] = {
#ifdef CONFIG_FAIR_GROUP_SCHED
	&cpu_share_attr.attr,
#endif
#ifdef CONFIG_RT_GROUP_SCHED
	&cpu_rt_runtime_attr.attr,
	&cpu_rt_period_attr.attr,
#endif
	NULL
};

/* the lifetime of user_struct is not managed by the core (now) */
static void uids_release(struct kobject *kobj)
{
	return;
}

static struct kobj_type uids_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_attrs = uids_attributes,
	.release = uids_release,
};

/*
 * Create /sys/kernel/uids/<uid>/cpu_share file for this user
 * We do not create this file for users in a user namespace (until
 * sysfs tagging is implemented).
 *
 * See Documentation/scheduler/sched-design-CFS.txt for ramifications.
 */
static int uids_user_create(struct user_struct *up)
{
	struct kobject *kobj = &up->kobj;
	int error;

	memset(kobj, 0, sizeof(struct kobject));
	if (up->user_ns != &init_user_ns)
		return 0;
	kobj->kset = uids_kset;
	error = kobject_init_and_add(kobj, &uids_ktype, NULL, "%d", up->uid);
	if (error) {
		kobject_put(kobj);
		goto done;
	}

	kobject_uevent(kobj, KOBJ_ADD);
done:
	return error;
}

/* create these entries in sysfs:
 * 	"/sys/kernel/uids" directory
 * 	"/sys/kernel/uids/0" directory (for root user)
 * 	"/sys/kernel/uids/0/cpu_share" file (for root user)
 */
int __init uids_sysfs_init(void)
{
	uids_kset = kset_create_and_add("uids", NULL, kernel_kobj);
	if (!uids_kset)
		return -ENOMEM;

	return uids_user_create(&root_user);
}

/* work function to remove sysfs directory for a user and free up
 * corresponding structures.
 */
static void cleanup_user_struct(struct work_struct *w)
{
	struct user_struct *up = container_of(w, struct user_struct, work);
	unsigned long flags;
	int remove_user = 0;

	/* Make uid_hash_remove() + sysfs_remove_file() + kobject_del()
	 * atomic.
	 */
	uids_mutex_lock();

	local_irq_save(flags);

	if (atomic_dec_and_lock(&up->__count, &uidhash_lock)) {
		uid_hash_remove(up);
		remove_user = 1;
		spin_unlock_irqrestore(&uidhash_lock, flags);
	} else {
		local_irq_restore(flags);
	}

	if (!remove_user)
		goto done;

	if (up->user_ns == &init_user_ns) {
		kobject_uevent(&up->kobj, KOBJ_REMOVE);
		kobject_del(&up->kobj);
		kobject_put(&up->kobj);
	}

	sched_destroy_user(up);
	key_put(up->uid_keyring);
	key_put(up->session_keyring);
	kmem_cache_free(uid_cachep, up);

done:
	uids_mutex_unlock();
}

/* IRQs are disabled and uidhash_lock is held upon function entry.
 * IRQ state (as stored in flags) is restored and uidhash_lock released
 * upon function exit.
 */
static void free_user(struct user_struct *up, unsigned long flags)
{
	/* restore back the count */
	atomic_inc(&up->__count);
	spin_unlock_irqrestore(&uidhash_lock, flags);

	INIT_WORK(&up->work, cleanup_user_struct);
	schedule_work(&up->work);
}

#else	/* CONFIG_USER_SCHED && CONFIG_SYSFS */

int uids_sysfs_init(void) { return 0; }
static inline int uids_user_create(struct user_struct *up) { return 0; }
static inline void uids_mutex_lock(void) { }
static inline void uids_mutex_unlock(void) { }

/* IRQs are disabled and uidhash_lock is held upon function entry.
 * IRQ state (as stored in flags) is restored and uidhash_lock released
 * upon function exit.
 */
static void free_user(struct user_struct *up, unsigned long flags)
{
	uid_hash_remove(up);
	spin_unlock_irqrestore(&uidhash_lock, flags);
	sched_destroy_user(up);
	key_put(up->uid_keyring);
	key_put(up->session_keyring);
	kmem_cache_free(uid_cachep, up);
}

#endif

#if defined(CONFIG_RT_GROUP_SCHED) && defined(CONFIG_USER_SCHED)
/*
 * We need to check if a setuid can take place. This function should be called
 * before successfully completing the setuid.
 */
int task_can_switch_user(struct user_struct *up, struct task_struct *tsk)
{

	return sched_rt_can_attach(up->tg, tsk);

}
#else
int task_can_switch_user(struct user_struct *up, struct task_struct *tsk)
{
	return 1;
}
#endif

/*
 * Locate the user_struct for the passed UID.  If found, take a ref on it.  The
 * caller must undo that ref with free_uid().
 *
 * If the user_struct could not be found, return NULL.
 */
struct user_struct *find_user(uid_t uid)
{
	struct user_struct *ret;
	unsigned long flags;
	struct user_namespace *ns = current_user_ns();

	spin_lock_irqsave(&uidhash_lock, flags);
	ret = uid_hash_find(uid, uidhashentry(ns, uid));
	spin_unlock_irqrestore(&uidhash_lock, flags);
	return ret;
}

void free_uid(struct user_struct *up)
{
	unsigned long flags;

	if (!up)
		return;

	local_irq_save(flags);
	if (atomic_dec_and_lock(&up->__count, &uidhash_lock))
		free_user(up, flags);
	else
		local_irq_restore(flags);
}

struct user_struct *alloc_uid(struct user_namespace *ns, uid_t uid)
{
	struct hlist_head *hashent = uidhashentry(ns, uid);
	struct user_struct *up, *new;

	/* Make uid_hash_find() + uids_user_create() + uid_hash_insert()
	 * atomic.
	 */
	uids_mutex_lock();

	spin_lock_irq(&uidhash_lock);
	up = uid_hash_find(uid, hashent);
	spin_unlock_irq(&uidhash_lock);

	if (!up) {
		new = kmem_cache_zalloc(uid_cachep, GFP_KERNEL);
		if (!new)
			goto out_unlock;

		new->uid = uid;
		atomic_set(&new->__count, 1);

		if (sched_create_user(new) < 0)
			goto out_free_user;

		new->user_ns = get_user_ns(ns);

		if (uids_user_create(new))
			goto out_destoy_sched;

		/*
		 * Before adding this, check whether we raced
		 * on adding the same user already..
		 */
		spin_lock_irq(&uidhash_lock);
		up = uid_hash_find(uid, hashent);
		if (up) {
			/* This case is not possible when CONFIG_USER_SCHED
			 * is defined, since we serialize alloc_uid() using
			 * uids_mutex. Hence no need to call
			 * sched_destroy_user() or remove_user_sysfs_dir().
			 */
			key_put(new->uid_keyring);
			key_put(new->session_keyring);
			kmem_cache_free(uid_cachep, new);
		} else {
			uid_hash_insert(new, hashent);
			up = new;
		}
		spin_unlock_irq(&uidhash_lock);
	}

	uids_mutex_unlock();

	return up;

out_destoy_sched:
	sched_destroy_user(new);
	put_user_ns(new->user_ns);
out_free_user:
	kmem_cache_free(uid_cachep, new);
out_unlock:
	uids_mutex_unlock();
	return NULL;
}

static int __init uid_cache_init(void)
{
	int n;

	uid_cachep = kmem_cache_create("uid_cache", sizeof(struct user_struct),
			0, SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);

	for(n = 0; n < UIDHASH_SZ; ++n)
		INIT_HLIST_HEAD(init_user_ns.uidhash_table + n);

	/* Insert the root user immediately (init already runs as root) */
	spin_lock_irq(&uidhash_lock);
	uid_hash_insert(&root_user, uidhashentry(&init_user_ns, 0));
	spin_unlock_irq(&uidhash_lock);

	return 0;
}

module_init(uid_cache_init);
