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

struct user_namespace init_user_ns = {
	.kref = {
		.refcount	= ATOMIC_INIT(2),
	},
	.root_user = &root_user,
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

struct user_struct root_user = {
	.__count	= ATOMIC_INIT(1),
	.processes	= ATOMIC_INIT(1),
	.files		= ATOMIC_INIT(0),
	.sigpending	= ATOMIC_INIT(0),
	.locked_shm     = 0,
#ifdef CONFIG_KEYS
	.uid_keyring	= &root_user_keyring,
	.session_keyring = &root_session_keyring,
#endif
#ifdef CONFIG_FAIR_USER_SCHED
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

#ifdef CONFIG_FAIR_USER_SCHED

static void sched_destroy_user(struct user_struct *up)
{
	sched_destroy_group(up->tg);
}

static int sched_create_user(struct user_struct *up)
{
	int rc = 0;

	up->tg = sched_create_group();
	if (IS_ERR(up->tg))
		rc = -ENOMEM;

	return rc;
}

static void sched_switch_user(struct task_struct *p)
{
	sched_move_task(p);
}

#else	/* CONFIG_FAIR_USER_SCHED */

static void sched_destroy_user(struct user_struct *up) { }
static int sched_create_user(struct user_struct *up) { return 0; }
static void sched_switch_user(struct task_struct *p) { }

#endif	/* CONFIG_FAIR_USER_SCHED */

#if defined(CONFIG_FAIR_USER_SCHED) && defined(CONFIG_SYSFS)

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

/* default attributes per uid directory */
static struct attribute *uids_attributes[] = {
	&cpu_share_attr.attr,
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

/* create /sys/kernel/uids/<uid>/cpu_share file for this user */
static int uids_user_create(struct user_struct *up)
{
	struct kobject *kobj = &up->kobj;
	int error;

	memset(kobj, 0, sizeof(struct kobject));
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
static void remove_user_sysfs_dir(struct work_struct *w)
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

	kobject_uevent(&up->kobj, KOBJ_REMOVE);
	kobject_del(&up->kobj);
	kobject_put(&up->kobj);

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
static inline void free_user(struct user_struct *up, unsigned long flags)
{
	/* restore back the count */
	atomic_inc(&up->__count);
	spin_unlock_irqrestore(&uidhash_lock, flags);

	INIT_WORK(&up->work, remove_user_sysfs_dir);
	schedule_work(&up->work);
}

#else	/* CONFIG_FAIR_USER_SCHED && CONFIG_SYSFS */

int uids_sysfs_init(void) { return 0; }
static inline int uids_user_create(struct user_struct *up) { return 0; }
static inline void uids_mutex_lock(void) { }
static inline void uids_mutex_unlock(void) { }

/* IRQs are disabled and uidhash_lock is held upon function entry.
 * IRQ state (as stored in flags) is restored and uidhash_lock released
 * upon function exit.
 */
static inline void free_user(struct user_struct *up, unsigned long flags)
{
	uid_hash_remove(up);
	spin_unlock_irqrestore(&uidhash_lock, flags);
	sched_destroy_user(up);
	key_put(up->uid_keyring);
	key_put(up->session_keyring);
	kmem_cache_free(uid_cachep, up);
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
	struct user_namespace *ns = current->nsproxy->user_ns;

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

struct user_struct * alloc_uid(struct user_namespace *ns, uid_t uid)
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
		new = kmem_cache_alloc(uid_cachep, GFP_KERNEL);
		if (!new)
			goto out_unlock;

		new->uid = uid;
		atomic_set(&new->__count, 1);
		atomic_set(&new->processes, 0);
		atomic_set(&new->files, 0);
		atomic_set(&new->sigpending, 0);
#ifdef CONFIG_INOTIFY_USER
		atomic_set(&new->inotify_watches, 0);
		atomic_set(&new->inotify_devs, 0);
#endif
#ifdef CONFIG_POSIX_MQUEUE
		new->mq_bytes = 0;
#endif
		new->locked_shm = 0;

		if (alloc_uid_keyring(new, current) < 0)
			goto out_free_user;

		if (sched_create_user(new) < 0)
			goto out_put_keys;

		if (uids_user_create(new))
			goto out_destoy_sched;

		/*
		 * Before adding this, check whether we raced
		 * on adding the same user already..
		 */
		spin_lock_irq(&uidhash_lock);
		up = uid_hash_find(uid, hashent);
		if (up) {
			/* This case is not possible when CONFIG_FAIR_USER_SCHED
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
out_put_keys:
	key_put(new->uid_keyring);
	key_put(new->session_keyring);
out_free_user:
	kmem_cache_free(uid_cachep, new);
out_unlock:
	uids_mutex_unlock();
	return NULL;
}

void switch_uid(struct user_struct *new_user)
{
	struct user_struct *old_user;

	/* What if a process setreuid()'s and this brings the
	 * new uid over his NPROC rlimit?  We can check this now
	 * cheaply with the new uid cache, so if it matters
	 * we should be checking for it.  -DaveM
	 */
	old_user = current->user;
	atomic_inc(&new_user->processes);
	atomic_dec(&old_user->processes);
	switch_uid_keyring(new_user);
	current->user = new_user;
	sched_switch_user(current);

	/*
	 * We need to synchronize with __sigqueue_alloc()
	 * doing a get_uid(p->user).. If that saw the old
	 * user value, we need to wait until it has exited
	 * its critical region before we can free the old
	 * structure.
	 */
	smp_mb();
	spin_unlock_wait(&current->sighand->siglock);

	free_uid(old_user);
	suid_keys(current);
}

#ifdef CONFIG_USER_NS
void release_uids(struct user_namespace *ns)
{
	int i;
	unsigned long flags;
	struct hlist_head *head;
	struct hlist_node *nd;

	spin_lock_irqsave(&uidhash_lock, flags);
	/*
	 * collapse the chains so that the user_struct-s will
	 * be still alive, but not in hashes. subsequent free_uid()
	 * will free them.
	 */
	for (i = 0; i < UIDHASH_SZ; i++) {
		head = ns->uidhash_table + i;
		while (!hlist_empty(head)) {
			nd = head->first;
			hlist_del_init(nd);
		}
	}
	spin_unlock_irqrestore(&uidhash_lock, flags);

	free_uid(ns->root_user);
}
#endif

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
