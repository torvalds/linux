// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/namespace.c
 *
 * (C) Copyright Al Viro 2000, 2001
 *
 * Based on code from fs/super.c, copyright Linus Torvalds and others.
 * Heavily rewritten.
 */

#include <linux/syscalls.h>
#include <linux/export.h>
#include <linux/capability.h>
#include <linux/mnt_namespace.h>
#include <linux/user_namespace.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/cred.h>
#include <linux/idr.h>
#include <linux/init.h>		/* init_rootfs */
#include <linux/fs_struct.h>	/* get_fs_root et.al. */
#include <linux/fsnotify.h>	/* fsnotify_vfsmount_delete */
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/proc_ns.h>
#include <linux/magic.h>
#include <linux/memblock.h>
#include <linux/proc_fs.h>
#include <linux/task_work.h>
#include <linux/sched/task.h>
#include <uapi/linux/mount.h>
#include <linux/fs_context.h>
#include <linux/shmem_fs.h>
#include <linux/mnt_idmapping.h>
#include <linux/pidfs.h>

#include "pnode.h"
#include "internal.h"

/* Maximum number of mounts in a mount namespace */
static unsigned int sysctl_mount_max __read_mostly = 100000;

static unsigned int m_hash_mask __ro_after_init;
static unsigned int m_hash_shift __ro_after_init;
static unsigned int mp_hash_mask __ro_after_init;
static unsigned int mp_hash_shift __ro_after_init;

static __initdata unsigned long mhash_entries;
static int __init set_mhash_entries(char *str)
{
	if (!str)
		return 0;
	mhash_entries = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("mhash_entries=", set_mhash_entries);

static __initdata unsigned long mphash_entries;
static int __init set_mphash_entries(char *str)
{
	if (!str)
		return 0;
	mphash_entries = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("mphash_entries=", set_mphash_entries);

static u64 event;
static DEFINE_XARRAY_FLAGS(mnt_id_xa, XA_FLAGS_ALLOC);
static DEFINE_IDA(mnt_group_ida);

/* Don't allow confusion with old 32bit mount ID */
#define MNT_UNIQUE_ID_OFFSET (1ULL << 31)
static u64 mnt_id_ctr = MNT_UNIQUE_ID_OFFSET;

static struct hlist_head *mount_hashtable __ro_after_init;
static struct hlist_head *mountpoint_hashtable __ro_after_init;
static struct kmem_cache *mnt_cache __ro_after_init;
static DECLARE_RWSEM(namespace_sem);
static HLIST_HEAD(unmounted);	/* protected by namespace_sem */
static LIST_HEAD(ex_mountpoints); /* protected by namespace_sem */
static struct mnt_namespace *emptied_ns; /* protected by namespace_sem */
static DEFINE_SEQLOCK(mnt_ns_tree_lock);

#ifdef CONFIG_FSNOTIFY
LIST_HEAD(notify_list); /* protected by namespace_sem */
#endif
static struct rb_root mnt_ns_tree = RB_ROOT; /* protected by mnt_ns_tree_lock */
static LIST_HEAD(mnt_ns_list); /* protected by mnt_ns_tree_lock */

enum mount_kattr_flags_t {
	MOUNT_KATTR_RECURSE		= (1 << 0),
	MOUNT_KATTR_IDMAP_REPLACE	= (1 << 1),
};

struct mount_kattr {
	unsigned int attr_set;
	unsigned int attr_clr;
	unsigned int propagation;
	unsigned int lookup_flags;
	enum mount_kattr_flags_t kflags;
	struct user_namespace *mnt_userns;
	struct mnt_idmap *mnt_idmap;
};

/* /sys/fs */
struct kobject *fs_kobj __ro_after_init;
EXPORT_SYMBOL_GPL(fs_kobj);

/*
 * vfsmount lock may be taken for read to prevent changes to the
 * vfsmount hash, ie. during mountpoint lookups or walking back
 * up the tree.
 *
 * It should be taken for write in all cases where the vfsmount
 * tree or hash is modified or when a vfsmount structure is modified.
 */
__cacheline_aligned_in_smp DEFINE_SEQLOCK(mount_lock);

static inline struct mnt_namespace *node_to_mnt_ns(const struct rb_node *node)
{
	if (!node)
		return NULL;
	return rb_entry(node, struct mnt_namespace, mnt_ns_tree_node);
}

static int mnt_ns_cmp(struct rb_node *a, const struct rb_node *b)
{
	struct mnt_namespace *ns_a = node_to_mnt_ns(a);
	struct mnt_namespace *ns_b = node_to_mnt_ns(b);
	u64 seq_a = ns_a->seq;
	u64 seq_b = ns_b->seq;

	if (seq_a < seq_b)
		return -1;
	if (seq_a > seq_b)
		return 1;
	return 0;
}

static inline void mnt_ns_tree_write_lock(void)
{
	write_seqlock(&mnt_ns_tree_lock);
}

static inline void mnt_ns_tree_write_unlock(void)
{
	write_sequnlock(&mnt_ns_tree_lock);
}

static void mnt_ns_tree_add(struct mnt_namespace *ns)
{
	struct rb_node *node, *prev;

	mnt_ns_tree_write_lock();
	node = rb_find_add_rcu(&ns->mnt_ns_tree_node, &mnt_ns_tree, mnt_ns_cmp);
	/*
	 * If there's no previous entry simply add it after the
	 * head and if there is add it after the previous entry.
	 */
	prev = rb_prev(&ns->mnt_ns_tree_node);
	if (!prev)
		list_add_rcu(&ns->mnt_ns_list, &mnt_ns_list);
	else
		list_add_rcu(&ns->mnt_ns_list, &node_to_mnt_ns(prev)->mnt_ns_list);
	mnt_ns_tree_write_unlock();

	WARN_ON_ONCE(node);
}

static void mnt_ns_release(struct mnt_namespace *ns)
{
	/* keep alive for {list,stat}mount() */
	if (refcount_dec_and_test(&ns->passive)) {
		fsnotify_mntns_delete(ns);
		put_user_ns(ns->user_ns);
		kfree(ns);
	}
}
DEFINE_FREE(mnt_ns_release, struct mnt_namespace *, if (_T) mnt_ns_release(_T))

static void mnt_ns_release_rcu(struct rcu_head *rcu)
{
	mnt_ns_release(container_of(rcu, struct mnt_namespace, mnt_ns_rcu));
}

static void mnt_ns_tree_remove(struct mnt_namespace *ns)
{
	/* remove from global mount namespace list */
	if (!is_anon_ns(ns)) {
		mnt_ns_tree_write_lock();
		rb_erase(&ns->mnt_ns_tree_node, &mnt_ns_tree);
		list_bidir_del_rcu(&ns->mnt_ns_list);
		mnt_ns_tree_write_unlock();
	}

	call_rcu(&ns->mnt_ns_rcu, mnt_ns_release_rcu);
}

static int mnt_ns_find(const void *key, const struct rb_node *node)
{
	const u64 mnt_ns_id = *(u64 *)key;
	const struct mnt_namespace *ns = node_to_mnt_ns(node);

	if (mnt_ns_id < ns->seq)
		return -1;
	if (mnt_ns_id > ns->seq)
		return 1;
	return 0;
}

/*
 * Lookup a mount namespace by id and take a passive reference count. Taking a
 * passive reference means the mount namespace can be emptied if e.g., the last
 * task holding an active reference exits. To access the mounts of the
 * namespace the @namespace_sem must first be acquired. If the namespace has
 * already shut down before acquiring @namespace_sem, {list,stat}mount() will
 * see that the mount rbtree of the namespace is empty.
 *
 * Note the lookup is lockless protected by a sequence counter. We only
 * need to guard against false negatives as false positives aren't
 * possible. So if we didn't find a mount namespace and the sequence
 * counter has changed we need to retry. If the sequence counter is
 * still the same we know the search actually failed.
 */
static struct mnt_namespace *lookup_mnt_ns(u64 mnt_ns_id)
{
	struct mnt_namespace *ns;
	struct rb_node *node;
	unsigned int seq;

	guard(rcu)();
	do {
		seq = read_seqbegin(&mnt_ns_tree_lock);
		node = rb_find_rcu(&mnt_ns_id, &mnt_ns_tree, mnt_ns_find);
		if (node)
			break;
	} while (read_seqretry(&mnt_ns_tree_lock, seq));

	if (!node)
		return NULL;

	/*
	 * The last reference count is put with RCU delay so we can
	 * unconditonally acquire a reference here.
	 */
	ns = node_to_mnt_ns(node);
	refcount_inc(&ns->passive);
	return ns;
}

static inline void lock_mount_hash(void)
{
	write_seqlock(&mount_lock);
}

static inline void unlock_mount_hash(void)
{
	write_sequnlock(&mount_lock);
}

static inline struct hlist_head *m_hash(struct vfsmount *mnt, struct dentry *dentry)
{
	unsigned long tmp = ((unsigned long)mnt / L1_CACHE_BYTES);
	tmp += ((unsigned long)dentry / L1_CACHE_BYTES);
	tmp = tmp + (tmp >> m_hash_shift);
	return &mount_hashtable[tmp & m_hash_mask];
}

static inline struct hlist_head *mp_hash(struct dentry *dentry)
{
	unsigned long tmp = ((unsigned long)dentry / L1_CACHE_BYTES);
	tmp = tmp + (tmp >> mp_hash_shift);
	return &mountpoint_hashtable[tmp & mp_hash_mask];
}

static int mnt_alloc_id(struct mount *mnt)
{
	int res;

	xa_lock(&mnt_id_xa);
	res = __xa_alloc(&mnt_id_xa, &mnt->mnt_id, mnt, XA_LIMIT(1, INT_MAX), GFP_KERNEL);
	if (!res)
		mnt->mnt_id_unique = ++mnt_id_ctr;
	xa_unlock(&mnt_id_xa);
	return res;
}

static void mnt_free_id(struct mount *mnt)
{
	xa_erase(&mnt_id_xa, mnt->mnt_id);
}

/*
 * Allocate a new peer group ID
 */
static int mnt_alloc_group_id(struct mount *mnt)
{
	int res = ida_alloc_min(&mnt_group_ida, 1, GFP_KERNEL);

	if (res < 0)
		return res;
	mnt->mnt_group_id = res;
	return 0;
}

/*
 * Release a peer group ID
 */
void mnt_release_group_id(struct mount *mnt)
{
	ida_free(&mnt_group_ida, mnt->mnt_group_id);
	mnt->mnt_group_id = 0;
}

/*
 * vfsmount lock must be held for read
 */
static inline void mnt_add_count(struct mount *mnt, int n)
{
#ifdef CONFIG_SMP
	this_cpu_add(mnt->mnt_pcp->mnt_count, n);
#else
	preempt_disable();
	mnt->mnt_count += n;
	preempt_enable();
#endif
}

/*
 * vfsmount lock must be held for write
 */
int mnt_get_count(struct mount *mnt)
{
#ifdef CONFIG_SMP
	int count = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		count += per_cpu_ptr(mnt->mnt_pcp, cpu)->mnt_count;
	}

	return count;
#else
	return mnt->mnt_count;
#endif
}

static struct mount *alloc_vfsmnt(const char *name)
{
	struct mount *mnt = kmem_cache_zalloc(mnt_cache, GFP_KERNEL);
	if (mnt) {
		int err;

		err = mnt_alloc_id(mnt);
		if (err)
			goto out_free_cache;

		if (name)
			mnt->mnt_devname = kstrdup_const(name,
							 GFP_KERNEL_ACCOUNT);
		else
			mnt->mnt_devname = "none";
		if (!mnt->mnt_devname)
			goto out_free_id;

#ifdef CONFIG_SMP
		mnt->mnt_pcp = alloc_percpu(struct mnt_pcp);
		if (!mnt->mnt_pcp)
			goto out_free_devname;

		this_cpu_add(mnt->mnt_pcp->mnt_count, 1);
#else
		mnt->mnt_count = 1;
		mnt->mnt_writers = 0;
#endif

		INIT_HLIST_NODE(&mnt->mnt_hash);
		INIT_LIST_HEAD(&mnt->mnt_child);
		INIT_LIST_HEAD(&mnt->mnt_mounts);
		INIT_LIST_HEAD(&mnt->mnt_list);
		INIT_LIST_HEAD(&mnt->mnt_expire);
		INIT_LIST_HEAD(&mnt->mnt_share);
		INIT_HLIST_HEAD(&mnt->mnt_slave_list);
		INIT_HLIST_NODE(&mnt->mnt_slave);
		INIT_HLIST_NODE(&mnt->mnt_mp_list);
		INIT_HLIST_HEAD(&mnt->mnt_stuck_children);
		RB_CLEAR_NODE(&mnt->mnt_node);
		mnt->mnt.mnt_idmap = &nop_mnt_idmap;
	}
	return mnt;

#ifdef CONFIG_SMP
out_free_devname:
	kfree_const(mnt->mnt_devname);
#endif
out_free_id:
	mnt_free_id(mnt);
out_free_cache:
	kmem_cache_free(mnt_cache, mnt);
	return NULL;
}

/*
 * Most r/o checks on a fs are for operations that take
 * discrete amounts of time, like a write() or unlink().
 * We must keep track of when those operations start
 * (for permission checks) and when they end, so that
 * we can determine when writes are able to occur to
 * a filesystem.
 */
/*
 * __mnt_is_readonly: check whether a mount is read-only
 * @mnt: the mount to check for its write status
 *
 * This shouldn't be used directly ouside of the VFS.
 * It does not guarantee that the filesystem will stay
 * r/w, just that it is right *now*.  This can not and
 * should not be used in place of IS_RDONLY(inode).
 * mnt_want/drop_write() will _keep_ the filesystem
 * r/w.
 */
bool __mnt_is_readonly(struct vfsmount *mnt)
{
	return (mnt->mnt_flags & MNT_READONLY) || sb_rdonly(mnt->mnt_sb);
}
EXPORT_SYMBOL_GPL(__mnt_is_readonly);

static inline void mnt_inc_writers(struct mount *mnt)
{
#ifdef CONFIG_SMP
	this_cpu_inc(mnt->mnt_pcp->mnt_writers);
#else
	mnt->mnt_writers++;
#endif
}

static inline void mnt_dec_writers(struct mount *mnt)
{
#ifdef CONFIG_SMP
	this_cpu_dec(mnt->mnt_pcp->mnt_writers);
#else
	mnt->mnt_writers--;
#endif
}

static unsigned int mnt_get_writers(struct mount *mnt)
{
#ifdef CONFIG_SMP
	unsigned int count = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		count += per_cpu_ptr(mnt->mnt_pcp, cpu)->mnt_writers;
	}

	return count;
#else
	return mnt->mnt_writers;
#endif
}

static int mnt_is_readonly(struct vfsmount *mnt)
{
	if (READ_ONCE(mnt->mnt_sb->s_readonly_remount))
		return 1;
	/*
	 * The barrier pairs with the barrier in sb_start_ro_state_change()
	 * making sure if we don't see s_readonly_remount set yet, we also will
	 * not see any superblock / mount flag changes done by remount.
	 * It also pairs with the barrier in sb_end_ro_state_change()
	 * assuring that if we see s_readonly_remount already cleared, we will
	 * see the values of superblock / mount flags updated by remount.
	 */
	smp_rmb();
	return __mnt_is_readonly(mnt);
}

/*
 * Most r/o & frozen checks on a fs are for operations that take discrete
 * amounts of time, like a write() or unlink().  We must keep track of when
 * those operations start (for permission checks) and when they end, so that we
 * can determine when writes are able to occur to a filesystem.
 */
/**
 * mnt_get_write_access - get write access to a mount without freeze protection
 * @m: the mount on which to take a write
 *
 * This tells the low-level filesystem that a write is about to be performed to
 * it, and makes sure that writes are allowed (mnt it read-write) before
 * returning success. This operation does not protect against filesystem being
 * frozen. When the write operation is finished, mnt_put_write_access() must be
 * called. This is effectively a refcount.
 */
int mnt_get_write_access(struct vfsmount *m)
{
	struct mount *mnt = real_mount(m);
	int ret = 0;

	preempt_disable();
	mnt_inc_writers(mnt);
	/*
	 * The store to mnt_inc_writers must be visible before we pass
	 * MNT_WRITE_HOLD loop below, so that the slowpath can see our
	 * incremented count after it has set MNT_WRITE_HOLD.
	 */
	smp_mb();
	might_lock(&mount_lock.lock);
	while (READ_ONCE(mnt->mnt.mnt_flags) & MNT_WRITE_HOLD) {
		if (!IS_ENABLED(CONFIG_PREEMPT_RT)) {
			cpu_relax();
		} else {
			/*
			 * This prevents priority inversion, if the task
			 * setting MNT_WRITE_HOLD got preempted on a remote
			 * CPU, and it prevents life lock if the task setting
			 * MNT_WRITE_HOLD has a lower priority and is bound to
			 * the same CPU as the task that is spinning here.
			 */
			preempt_enable();
			lock_mount_hash();
			unlock_mount_hash();
			preempt_disable();
		}
	}
	/*
	 * The barrier pairs with the barrier sb_start_ro_state_change() making
	 * sure that if we see MNT_WRITE_HOLD cleared, we will also see
	 * s_readonly_remount set (or even SB_RDONLY / MNT_READONLY flags) in
	 * mnt_is_readonly() and bail in case we are racing with remount
	 * read-only.
	 */
	smp_rmb();
	if (mnt_is_readonly(m)) {
		mnt_dec_writers(mnt);
		ret = -EROFS;
	}
	preempt_enable();

	return ret;
}
EXPORT_SYMBOL_GPL(mnt_get_write_access);

/**
 * mnt_want_write - get write access to a mount
 * @m: the mount on which to take a write
 *
 * This tells the low-level filesystem that a write is about to be performed to
 * it, and makes sure that writes are allowed (mount is read-write, filesystem
 * is not frozen) before returning success.  When the write operation is
 * finished, mnt_drop_write() must be called.  This is effectively a refcount.
 */
int mnt_want_write(struct vfsmount *m)
{
	int ret;

	sb_start_write(m->mnt_sb);
	ret = mnt_get_write_access(m);
	if (ret)
		sb_end_write(m->mnt_sb);
	return ret;
}
EXPORT_SYMBOL_GPL(mnt_want_write);

/**
 * mnt_get_write_access_file - get write access to a file's mount
 * @file: the file who's mount on which to take a write
 *
 * This is like mnt_get_write_access, but if @file is already open for write it
 * skips incrementing mnt_writers (since the open file already has a reference)
 * and instead only does the check for emergency r/o remounts.  This must be
 * paired with mnt_put_write_access_file.
 */
int mnt_get_write_access_file(struct file *file)
{
	if (file->f_mode & FMODE_WRITER) {
		/*
		 * Superblock may have become readonly while there are still
		 * writable fd's, e.g. due to a fs error with errors=remount-ro
		 */
		if (__mnt_is_readonly(file->f_path.mnt))
			return -EROFS;
		return 0;
	}
	return mnt_get_write_access(file->f_path.mnt);
}

/**
 * mnt_want_write_file - get write access to a file's mount
 * @file: the file who's mount on which to take a write
 *
 * This is like mnt_want_write, but if the file is already open for writing it
 * skips incrementing mnt_writers (since the open file already has a reference)
 * and instead only does the freeze protection and the check for emergency r/o
 * remounts.  This must be paired with mnt_drop_write_file.
 */
int mnt_want_write_file(struct file *file)
{
	int ret;

	sb_start_write(file_inode(file)->i_sb);
	ret = mnt_get_write_access_file(file);
	if (ret)
		sb_end_write(file_inode(file)->i_sb);
	return ret;
}
EXPORT_SYMBOL_GPL(mnt_want_write_file);

/**
 * mnt_put_write_access - give up write access to a mount
 * @mnt: the mount on which to give up write access
 *
 * Tells the low-level filesystem that we are done
 * performing writes to it.  Must be matched with
 * mnt_get_write_access() call above.
 */
void mnt_put_write_access(struct vfsmount *mnt)
{
	preempt_disable();
	mnt_dec_writers(real_mount(mnt));
	preempt_enable();
}
EXPORT_SYMBOL_GPL(mnt_put_write_access);

/**
 * mnt_drop_write - give up write access to a mount
 * @mnt: the mount on which to give up write access
 *
 * Tells the low-level filesystem that we are done performing writes to it and
 * also allows filesystem to be frozen again.  Must be matched with
 * mnt_want_write() call above.
 */
void mnt_drop_write(struct vfsmount *mnt)
{
	mnt_put_write_access(mnt);
	sb_end_write(mnt->mnt_sb);
}
EXPORT_SYMBOL_GPL(mnt_drop_write);

void mnt_put_write_access_file(struct file *file)
{
	if (!(file->f_mode & FMODE_WRITER))
		mnt_put_write_access(file->f_path.mnt);
}

void mnt_drop_write_file(struct file *file)
{
	mnt_put_write_access_file(file);
	sb_end_write(file_inode(file)->i_sb);
}
EXPORT_SYMBOL(mnt_drop_write_file);

/**
 * mnt_hold_writers - prevent write access to the given mount
 * @mnt: mnt to prevent write access to
 *
 * Prevents write access to @mnt if there are no active writers for @mnt.
 * This function needs to be called and return successfully before changing
 * properties of @mnt that need to remain stable for callers with write access
 * to @mnt.
 *
 * After this functions has been called successfully callers must pair it with
 * a call to mnt_unhold_writers() in order to stop preventing write access to
 * @mnt.
 *
 * Context: This function expects lock_mount_hash() to be held serializing
 *          setting MNT_WRITE_HOLD.
 * Return: On success 0 is returned.
 *	   On error, -EBUSY is returned.
 */
static inline int mnt_hold_writers(struct mount *mnt)
{
	mnt->mnt.mnt_flags |= MNT_WRITE_HOLD;
	/*
	 * After storing MNT_WRITE_HOLD, we'll read the counters. This store
	 * should be visible before we do.
	 */
	smp_mb();

	/*
	 * With writers on hold, if this value is zero, then there are
	 * definitely no active writers (although held writers may subsequently
	 * increment the count, they'll have to wait, and decrement it after
	 * seeing MNT_READONLY).
	 *
	 * It is OK to have counter incremented on one CPU and decremented on
	 * another: the sum will add up correctly. The danger would be when we
	 * sum up each counter, if we read a counter before it is incremented,
	 * but then read another CPU's count which it has been subsequently
	 * decremented from -- we would see more decrements than we should.
	 * MNT_WRITE_HOLD protects against this scenario, because
	 * mnt_want_write first increments count, then smp_mb, then spins on
	 * MNT_WRITE_HOLD, so it can't be decremented by another CPU while
	 * we're counting up here.
	 */
	if (mnt_get_writers(mnt) > 0)
		return -EBUSY;

	return 0;
}

/**
 * mnt_unhold_writers - stop preventing write access to the given mount
 * @mnt: mnt to stop preventing write access to
 *
 * Stop preventing write access to @mnt allowing callers to gain write access
 * to @mnt again.
 *
 * This function can only be called after a successful call to
 * mnt_hold_writers().
 *
 * Context: This function expects lock_mount_hash() to be held.
 */
static inline void mnt_unhold_writers(struct mount *mnt)
{
	/*
	 * MNT_READONLY must become visible before ~MNT_WRITE_HOLD, so writers
	 * that become unheld will see MNT_READONLY.
	 */
	smp_wmb();
	mnt->mnt.mnt_flags &= ~MNT_WRITE_HOLD;
}

static int mnt_make_readonly(struct mount *mnt)
{
	int ret;

	ret = mnt_hold_writers(mnt);
	if (!ret)
		mnt->mnt.mnt_flags |= MNT_READONLY;
	mnt_unhold_writers(mnt);
	return ret;
}

int sb_prepare_remount_readonly(struct super_block *sb)
{
	struct mount *mnt;
	int err = 0;

	/* Racy optimization.  Recheck the counter under MNT_WRITE_HOLD */
	if (atomic_long_read(&sb->s_remove_count))
		return -EBUSY;

	lock_mount_hash();
	list_for_each_entry(mnt, &sb->s_mounts, mnt_instance) {
		if (!(mnt->mnt.mnt_flags & MNT_READONLY)) {
			err = mnt_hold_writers(mnt);
			if (err)
				break;
		}
	}
	if (!err && atomic_long_read(&sb->s_remove_count))
		err = -EBUSY;

	if (!err)
		sb_start_ro_state_change(sb);
	list_for_each_entry(mnt, &sb->s_mounts, mnt_instance) {
		if (mnt->mnt.mnt_flags & MNT_WRITE_HOLD)
			mnt->mnt.mnt_flags &= ~MNT_WRITE_HOLD;
	}
	unlock_mount_hash();

	return err;
}

static void free_vfsmnt(struct mount *mnt)
{
	mnt_idmap_put(mnt_idmap(&mnt->mnt));
	kfree_const(mnt->mnt_devname);
#ifdef CONFIG_SMP
	free_percpu(mnt->mnt_pcp);
#endif
	kmem_cache_free(mnt_cache, mnt);
}

static void delayed_free_vfsmnt(struct rcu_head *head)
{
	free_vfsmnt(container_of(head, struct mount, mnt_rcu));
}

/* call under rcu_read_lock */
int __legitimize_mnt(struct vfsmount *bastard, unsigned seq)
{
	struct mount *mnt;
	if (read_seqretry(&mount_lock, seq))
		return 1;
	if (bastard == NULL)
		return 0;
	mnt = real_mount(bastard);
	mnt_add_count(mnt, 1);
	smp_mb();		// see mntput_no_expire() and do_umount()
	if (likely(!read_seqretry(&mount_lock, seq)))
		return 0;
	lock_mount_hash();
	if (unlikely(bastard->mnt_flags & (MNT_SYNC_UMOUNT | MNT_DOOMED))) {
		mnt_add_count(mnt, -1);
		unlock_mount_hash();
		return 1;
	}
	unlock_mount_hash();
	/* caller will mntput() */
	return -1;
}

/* call under rcu_read_lock */
static bool legitimize_mnt(struct vfsmount *bastard, unsigned seq)
{
	int res = __legitimize_mnt(bastard, seq);
	if (likely(!res))
		return true;
	if (unlikely(res < 0)) {
		rcu_read_unlock();
		mntput(bastard);
		rcu_read_lock();
	}
	return false;
}

/**
 * __lookup_mnt - find first child mount
 * @mnt:	parent mount
 * @dentry:	mountpoint
 *
 * If @mnt has a child mount @c mounted @dentry find and return it.
 *
 * Note that the child mount @c need not be unique. There are cases
 * where shadow mounts are created. For example, during mount
 * propagation when a source mount @mnt whose root got overmounted by a
 * mount @o after path lookup but before @namespace_sem could be
 * acquired gets copied and propagated. So @mnt gets copied including
 * @o. When @mnt is propagated to a destination mount @d that already
 * has another mount @n mounted at the same mountpoint then the source
 * mount @mnt will be tucked beneath @n, i.e., @n will be mounted on
 * @mnt and @mnt mounted on @d. Now both @n and @o are mounted at @mnt
 * on @dentry.
 *
 * Return: The first child of @mnt mounted @dentry or NULL.
 */
struct mount *__lookup_mnt(struct vfsmount *mnt, struct dentry *dentry)
{
	struct hlist_head *head = m_hash(mnt, dentry);
	struct mount *p;

	hlist_for_each_entry_rcu(p, head, mnt_hash)
		if (&p->mnt_parent->mnt == mnt && p->mnt_mountpoint == dentry)
			return p;
	return NULL;
}

/*
 * lookup_mnt - Return the first child mount mounted at path
 *
 * "First" means first mounted chronologically.  If you create the
 * following mounts:
 *
 * mount /dev/sda1 /mnt
 * mount /dev/sda2 /mnt
 * mount /dev/sda3 /mnt
 *
 * Then lookup_mnt() on the base /mnt dentry in the root mount will
 * return successively the root dentry and vfsmount of /dev/sda1, then
 * /dev/sda2, then /dev/sda3, then NULL.
 *
 * lookup_mnt takes a reference to the found vfsmount.
 */
struct vfsmount *lookup_mnt(const struct path *path)
{
	struct mount *child_mnt;
	struct vfsmount *m;
	unsigned seq;

	rcu_read_lock();
	do {
		seq = read_seqbegin(&mount_lock);
		child_mnt = __lookup_mnt(path->mnt, path->dentry);
		m = child_mnt ? &child_mnt->mnt : NULL;
	} while (!legitimize_mnt(m, seq));
	rcu_read_unlock();
	return m;
}

/*
 * __is_local_mountpoint - Test to see if dentry is a mountpoint in the
 *                         current mount namespace.
 *
 * The common case is dentries are not mountpoints at all and that
 * test is handled inline.  For the slow case when we are actually
 * dealing with a mountpoint of some kind, walk through all of the
 * mounts in the current mount namespace and test to see if the dentry
 * is a mountpoint.
 *
 * The mount_hashtable is not usable in the context because we
 * need to identify all mounts that may be in the current mount
 * namespace not just a mount that happens to have some specified
 * parent mount.
 */
bool __is_local_mountpoint(const struct dentry *dentry)
{
	struct mnt_namespace *ns = current->nsproxy->mnt_ns;
	struct mount *mnt, *n;
	bool is_covered = false;

	down_read(&namespace_sem);
	rbtree_postorder_for_each_entry_safe(mnt, n, &ns->mounts, mnt_node) {
		is_covered = (mnt->mnt_mountpoint == dentry);
		if (is_covered)
			break;
	}
	up_read(&namespace_sem);

	return is_covered;
}

struct pinned_mountpoint {
	struct hlist_node node;
	struct mountpoint *mp;
};

static bool lookup_mountpoint(struct dentry *dentry, struct pinned_mountpoint *m)
{
	struct hlist_head *chain = mp_hash(dentry);
	struct mountpoint *mp;

	hlist_for_each_entry(mp, chain, m_hash) {
		if (mp->m_dentry == dentry) {
			hlist_add_head(&m->node, &mp->m_list);
			m->mp = mp;
			return true;
		}
	}
	return false;
}

static int get_mountpoint(struct dentry *dentry, struct pinned_mountpoint *m)
{
	struct mountpoint *mp __free(kfree) = NULL;
	bool found;
	int ret;

	if (d_mountpoint(dentry)) {
		/* might be worth a WARN_ON() */
		if (d_unlinked(dentry))
			return -ENOENT;
mountpoint:
		read_seqlock_excl(&mount_lock);
		found = lookup_mountpoint(dentry, m);
		read_sequnlock_excl(&mount_lock);
		if (found)
			return 0;
	}

	if (!mp)
		mp = kmalloc(sizeof(struct mountpoint), GFP_KERNEL);
	if (!mp)
		return -ENOMEM;

	/* Exactly one processes may set d_mounted */
	ret = d_set_mounted(dentry);

	/* Someone else set d_mounted? */
	if (ret == -EBUSY)
		goto mountpoint;

	/* The dentry is not available as a mountpoint? */
	if (ret)
		return ret;

	/* Add the new mountpoint to the hash table */
	read_seqlock_excl(&mount_lock);
	mp->m_dentry = dget(dentry);
	hlist_add_head(&mp->m_hash, mp_hash(dentry));
	INIT_HLIST_HEAD(&mp->m_list);
	hlist_add_head(&m->node, &mp->m_list);
	m->mp = no_free_ptr(mp);
	read_sequnlock_excl(&mount_lock);
	return 0;
}

/*
 * vfsmount lock must be held.  Additionally, the caller is responsible
 * for serializing calls for given disposal list.
 */
static void maybe_free_mountpoint(struct mountpoint *mp, struct list_head *list)
{
	if (hlist_empty(&mp->m_list)) {
		struct dentry *dentry = mp->m_dentry;
		spin_lock(&dentry->d_lock);
		dentry->d_flags &= ~DCACHE_MOUNTED;
		spin_unlock(&dentry->d_lock);
		dput_to_list(dentry, list);
		hlist_del(&mp->m_hash);
		kfree(mp);
	}
}

/*
 * locks: mount_lock [read_seqlock_excl], namespace_sem [excl]
 */
static void unpin_mountpoint(struct pinned_mountpoint *m)
{
	if (m->mp) {
		hlist_del(&m->node);
		maybe_free_mountpoint(m->mp, &ex_mountpoints);
	}
}

static inline int check_mnt(struct mount *mnt)
{
	return mnt->mnt_ns == current->nsproxy->mnt_ns;
}

static inline bool check_anonymous_mnt(struct mount *mnt)
{
	u64 seq;

	if (!is_anon_ns(mnt->mnt_ns))
		return false;

	seq = mnt->mnt_ns->seq_origin;
	return !seq || (seq == current->nsproxy->mnt_ns->seq);
}

/*
 * vfsmount lock must be held for write
 */
static void touch_mnt_namespace(struct mnt_namespace *ns)
{
	if (ns) {
		ns->event = ++event;
		wake_up_interruptible(&ns->poll);
	}
}

/*
 * vfsmount lock must be held for write
 */
static void __touch_mnt_namespace(struct mnt_namespace *ns)
{
	if (ns && ns->event != event) {
		ns->event = event;
		wake_up_interruptible(&ns->poll);
	}
}

/*
 * locks: mount_lock[write_seqlock]
 */
static void __umount_mnt(struct mount *mnt, struct list_head *shrink_list)
{
	struct mountpoint *mp;
	struct mount *parent = mnt->mnt_parent;
	if (unlikely(parent->overmount == mnt))
		parent->overmount = NULL;
	mnt->mnt_parent = mnt;
	mnt->mnt_mountpoint = mnt->mnt.mnt_root;
	list_del_init(&mnt->mnt_child);
	hlist_del_init_rcu(&mnt->mnt_hash);
	hlist_del_init(&mnt->mnt_mp_list);
	mp = mnt->mnt_mp;
	mnt->mnt_mp = NULL;
	maybe_free_mountpoint(mp, shrink_list);
}

/*
 * locks: mount_lock[write_seqlock], namespace_sem[excl] (for ex_mountpoints)
 */
static void umount_mnt(struct mount *mnt)
{
	__umount_mnt(mnt, &ex_mountpoints);
}

/*
 * vfsmount lock must be held for write
 */
void mnt_set_mountpoint(struct mount *mnt,
			struct mountpoint *mp,
			struct mount *child_mnt)
{
	child_mnt->mnt_mountpoint = mp->m_dentry;
	child_mnt->mnt_parent = mnt;
	child_mnt->mnt_mp = mp;
	hlist_add_head(&child_mnt->mnt_mp_list, &mp->m_list);
}

static void make_visible(struct mount *mnt)
{
	struct mount *parent = mnt->mnt_parent;
	if (unlikely(mnt->mnt_mountpoint == parent->mnt.mnt_root))
		parent->overmount = mnt;
	hlist_add_head_rcu(&mnt->mnt_hash,
			   m_hash(&parent->mnt, mnt->mnt_mountpoint));
	list_add_tail(&mnt->mnt_child, &parent->mnt_mounts);
}

/**
 * attach_mnt - mount a mount, attach to @mount_hashtable and parent's
 *              list of child mounts
 * @parent:  the parent
 * @mnt:     the new mount
 * @mp:      the new mountpoint
 *
 * Mount @mnt at @mp on @parent. Then attach @mnt
 * to @parent's child mount list and to @mount_hashtable.
 *
 * Note, when make_visible() is called @mnt->mnt_parent already points
 * to the correct parent.
 *
 * Context: This function expects namespace_lock() and lock_mount_hash()
 *          to have been acquired in that order.
 */
static void attach_mnt(struct mount *mnt, struct mount *parent,
		       struct mountpoint *mp)
{
	mnt_set_mountpoint(parent, mp, mnt);
	make_visible(mnt);
}

void mnt_change_mountpoint(struct mount *parent, struct mountpoint *mp, struct mount *mnt)
{
	struct mountpoint *old_mp = mnt->mnt_mp;

	list_del_init(&mnt->mnt_child);
	hlist_del_init(&mnt->mnt_mp_list);
	hlist_del_init_rcu(&mnt->mnt_hash);

	attach_mnt(mnt, parent, mp);

	maybe_free_mountpoint(old_mp, &ex_mountpoints);
}

static inline struct mount *node_to_mount(struct rb_node *node)
{
	return node ? rb_entry(node, struct mount, mnt_node) : NULL;
}

static void mnt_add_to_ns(struct mnt_namespace *ns, struct mount *mnt)
{
	struct rb_node **link = &ns->mounts.rb_node;
	struct rb_node *parent = NULL;
	bool mnt_first_node = true, mnt_last_node = true;

	WARN_ON(mnt_ns_attached(mnt));
	mnt->mnt_ns = ns;
	while (*link) {
		parent = *link;
		if (mnt->mnt_id_unique < node_to_mount(parent)->mnt_id_unique) {
			link = &parent->rb_left;
			mnt_last_node = false;
		} else {
			link = &parent->rb_right;
			mnt_first_node = false;
		}
	}

	if (mnt_last_node)
		ns->mnt_last_node = &mnt->mnt_node;
	if (mnt_first_node)
		ns->mnt_first_node = &mnt->mnt_node;
	rb_link_node(&mnt->mnt_node, parent, link);
	rb_insert_color(&mnt->mnt_node, &ns->mounts);

	mnt_notify_add(mnt);
}

static struct mount *next_mnt(struct mount *p, struct mount *root)
{
	struct list_head *next = p->mnt_mounts.next;
	if (next == &p->mnt_mounts) {
		while (1) {
			if (p == root)
				return NULL;
			next = p->mnt_child.next;
			if (next != &p->mnt_parent->mnt_mounts)
				break;
			p = p->mnt_parent;
		}
	}
	return list_entry(next, struct mount, mnt_child);
}

static struct mount *skip_mnt_tree(struct mount *p)
{
	struct list_head *prev = p->mnt_mounts.prev;
	while (prev != &p->mnt_mounts) {
		p = list_entry(prev, struct mount, mnt_child);
		prev = p->mnt_mounts.prev;
	}
	return p;
}

/*
 * vfsmount lock must be held for write
 */
static void commit_tree(struct mount *mnt)
{
	struct mnt_namespace *n = mnt->mnt_parent->mnt_ns;

	if (!mnt_ns_attached(mnt)) {
		for (struct mount *m = mnt; m; m = next_mnt(m, mnt))
			if (unlikely(mnt_ns_attached(m)))
				m = skip_mnt_tree(m);
			else
				mnt_add_to_ns(n, m);
		n->nr_mounts += n->pending_mounts;
		n->pending_mounts = 0;
	}

	make_visible(mnt);
	touch_mnt_namespace(n);
}

/**
 * vfs_create_mount - Create a mount for a configured superblock
 * @fc: The configuration context with the superblock attached
 *
 * Create a mount to an already configured superblock.  If necessary, the
 * caller should invoke vfs_get_tree() before calling this.
 *
 * Note that this does not attach the mount to anything.
 */
struct vfsmount *vfs_create_mount(struct fs_context *fc)
{
	struct mount *mnt;

	if (!fc->root)
		return ERR_PTR(-EINVAL);

	mnt = alloc_vfsmnt(fc->source);
	if (!mnt)
		return ERR_PTR(-ENOMEM);

	if (fc->sb_flags & SB_KERNMOUNT)
		mnt->mnt.mnt_flags = MNT_INTERNAL;

	atomic_inc(&fc->root->d_sb->s_active);
	mnt->mnt.mnt_sb		= fc->root->d_sb;
	mnt->mnt.mnt_root	= dget(fc->root);
	mnt->mnt_mountpoint	= mnt->mnt.mnt_root;
	mnt->mnt_parent		= mnt;

	lock_mount_hash();
	list_add_tail(&mnt->mnt_instance, &mnt->mnt.mnt_sb->s_mounts);
	unlock_mount_hash();
	return &mnt->mnt;
}
EXPORT_SYMBOL(vfs_create_mount);

struct vfsmount *fc_mount(struct fs_context *fc)
{
	int err = vfs_get_tree(fc);
	if (!err) {
		up_write(&fc->root->d_sb->s_umount);
		return vfs_create_mount(fc);
	}
	return ERR_PTR(err);
}
EXPORT_SYMBOL(fc_mount);

struct vfsmount *fc_mount_longterm(struct fs_context *fc)
{
	struct vfsmount *mnt = fc_mount(fc);
	if (!IS_ERR(mnt))
		real_mount(mnt)->mnt_ns = MNT_NS_INTERNAL;
	return mnt;
}
EXPORT_SYMBOL(fc_mount_longterm);

struct vfsmount *vfs_kern_mount(struct file_system_type *type,
				int flags, const char *name,
				void *data)
{
	struct fs_context *fc;
	struct vfsmount *mnt;
	int ret = 0;

	if (!type)
		return ERR_PTR(-EINVAL);

	fc = fs_context_for_mount(type, flags);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	if (name)
		ret = vfs_parse_fs_string(fc, "source",
					  name, strlen(name));
	if (!ret)
		ret = parse_monolithic_mount_data(fc, data);
	if (!ret)
		mnt = fc_mount(fc);
	else
		mnt = ERR_PTR(ret);

	put_fs_context(fc);
	return mnt;
}
EXPORT_SYMBOL_GPL(vfs_kern_mount);

static struct mount *clone_mnt(struct mount *old, struct dentry *root,
					int flag)
{
	struct super_block *sb = old->mnt.mnt_sb;
	struct mount *mnt;
	int err;

	mnt = alloc_vfsmnt(old->mnt_devname);
	if (!mnt)
		return ERR_PTR(-ENOMEM);

	mnt->mnt.mnt_flags = READ_ONCE(old->mnt.mnt_flags) &
			     ~MNT_INTERNAL_FLAGS;

	if (flag & (CL_SLAVE | CL_PRIVATE))
		mnt->mnt_group_id = 0; /* not a peer of original */
	else
		mnt->mnt_group_id = old->mnt_group_id;

	if ((flag & CL_MAKE_SHARED) && !mnt->mnt_group_id) {
		err = mnt_alloc_group_id(mnt);
		if (err)
			goto out_free;
	}

	if (mnt->mnt_group_id)
		set_mnt_shared(mnt);

	atomic_inc(&sb->s_active);
	mnt->mnt.mnt_idmap = mnt_idmap_get(mnt_idmap(&old->mnt));

	mnt->mnt.mnt_sb = sb;
	mnt->mnt.mnt_root = dget(root);
	mnt->mnt_mountpoint = mnt->mnt.mnt_root;
	mnt->mnt_parent = mnt;
	lock_mount_hash();
	list_add_tail(&mnt->mnt_instance, &sb->s_mounts);
	unlock_mount_hash();

	if (flag & CL_PRIVATE)	// we are done with it
		return mnt;

	if (peers(mnt, old))
		list_add(&mnt->mnt_share, &old->mnt_share);

	if ((flag & CL_SLAVE) && old->mnt_group_id) {
		hlist_add_head(&mnt->mnt_slave, &old->mnt_slave_list);
		mnt->mnt_master = old;
	} else if (IS_MNT_SLAVE(old)) {
		hlist_add_behind(&mnt->mnt_slave, &old->mnt_slave);
		mnt->mnt_master = old->mnt_master;
	}
	return mnt;

 out_free:
	mnt_free_id(mnt);
	free_vfsmnt(mnt);
	return ERR_PTR(err);
}

static void cleanup_mnt(struct mount *mnt)
{
	struct hlist_node *p;
	struct mount *m;
	/*
	 * The warning here probably indicates that somebody messed
	 * up a mnt_want/drop_write() pair.  If this happens, the
	 * filesystem was probably unable to make r/w->r/o transitions.
	 * The locking used to deal with mnt_count decrement provides barriers,
	 * so mnt_get_writers() below is safe.
	 */
	WARN_ON(mnt_get_writers(mnt));
	if (unlikely(mnt->mnt_pins.first))
		mnt_pin_kill(mnt);
	hlist_for_each_entry_safe(m, p, &mnt->mnt_stuck_children, mnt_umount) {
		hlist_del(&m->mnt_umount);
		mntput(&m->mnt);
	}
	fsnotify_vfsmount_delete(&mnt->mnt);
	dput(mnt->mnt.mnt_root);
	deactivate_super(mnt->mnt.mnt_sb);
	mnt_free_id(mnt);
	call_rcu(&mnt->mnt_rcu, delayed_free_vfsmnt);
}

static void __cleanup_mnt(struct rcu_head *head)
{
	cleanup_mnt(container_of(head, struct mount, mnt_rcu));
}

static LLIST_HEAD(delayed_mntput_list);
static void delayed_mntput(struct work_struct *unused)
{
	struct llist_node *node = llist_del_all(&delayed_mntput_list);
	struct mount *m, *t;

	llist_for_each_entry_safe(m, t, node, mnt_llist)
		cleanup_mnt(m);
}
static DECLARE_DELAYED_WORK(delayed_mntput_work, delayed_mntput);

static void mntput_no_expire(struct mount *mnt)
{
	LIST_HEAD(list);
	int count;

	rcu_read_lock();
	if (likely(READ_ONCE(mnt->mnt_ns))) {
		/*
		 * Since we don't do lock_mount_hash() here,
		 * ->mnt_ns can change under us.  However, if it's
		 * non-NULL, then there's a reference that won't
		 * be dropped until after an RCU delay done after
		 * turning ->mnt_ns NULL.  So if we observe it
		 * non-NULL under rcu_read_lock(), the reference
		 * we are dropping is not the final one.
		 */
		mnt_add_count(mnt, -1);
		rcu_read_unlock();
		return;
	}
	lock_mount_hash();
	/*
	 * make sure that if __legitimize_mnt() has not seen us grab
	 * mount_lock, we'll see their refcount increment here.
	 */
	smp_mb();
	mnt_add_count(mnt, -1);
	count = mnt_get_count(mnt);
	if (count != 0) {
		WARN_ON(count < 0);
		rcu_read_unlock();
		unlock_mount_hash();
		return;
	}
	if (unlikely(mnt->mnt.mnt_flags & MNT_DOOMED)) {
		rcu_read_unlock();
		unlock_mount_hash();
		return;
	}
	mnt->mnt.mnt_flags |= MNT_DOOMED;
	rcu_read_unlock();

	list_del(&mnt->mnt_instance);
	if (unlikely(!list_empty(&mnt->mnt_expire)))
		list_del(&mnt->mnt_expire);

	if (unlikely(!list_empty(&mnt->mnt_mounts))) {
		struct mount *p, *tmp;
		list_for_each_entry_safe(p, tmp, &mnt->mnt_mounts,  mnt_child) {
			__umount_mnt(p, &list);
			hlist_add_head(&p->mnt_umount, &mnt->mnt_stuck_children);
		}
	}
	unlock_mount_hash();
	shrink_dentry_list(&list);

	if (likely(!(mnt->mnt.mnt_flags & MNT_INTERNAL))) {
		struct task_struct *task = current;
		if (likely(!(task->flags & PF_KTHREAD))) {
			init_task_work(&mnt->mnt_rcu, __cleanup_mnt);
			if (!task_work_add(task, &mnt->mnt_rcu, TWA_RESUME))
				return;
		}
		if (llist_add(&mnt->mnt_llist, &delayed_mntput_list))
			schedule_delayed_work(&delayed_mntput_work, 1);
		return;
	}
	cleanup_mnt(mnt);
}

void mntput(struct vfsmount *mnt)
{
	if (mnt) {
		struct mount *m = real_mount(mnt);
		/* avoid cacheline pingpong */
		if (unlikely(m->mnt_expiry_mark))
			WRITE_ONCE(m->mnt_expiry_mark, 0);
		mntput_no_expire(m);
	}
}
EXPORT_SYMBOL(mntput);

struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		mnt_add_count(real_mount(mnt), 1);
	return mnt;
}
EXPORT_SYMBOL(mntget);

/*
 * Make a mount point inaccessible to new lookups.
 * Because there may still be current users, the caller MUST WAIT
 * for an RCU grace period before destroying the mount point.
 */
void mnt_make_shortterm(struct vfsmount *mnt)
{
	if (mnt)
		real_mount(mnt)->mnt_ns = NULL;
}

/**
 * path_is_mountpoint() - Check if path is a mount in the current namespace.
 * @path: path to check
 *
 *  d_mountpoint() can only be used reliably to establish if a dentry is
 *  not mounted in any namespace and that common case is handled inline.
 *  d_mountpoint() isn't aware of the possibility there may be multiple
 *  mounts using a given dentry in a different namespace. This function
 *  checks if the passed in path is a mountpoint rather than the dentry
 *  alone.
 */
bool path_is_mountpoint(const struct path *path)
{
	unsigned seq;
	bool res;

	if (!d_mountpoint(path->dentry))
		return false;

	rcu_read_lock();
	do {
		seq = read_seqbegin(&mount_lock);
		res = __path_is_mountpoint(path);
	} while (read_seqretry(&mount_lock, seq));
	rcu_read_unlock();

	return res;
}
EXPORT_SYMBOL(path_is_mountpoint);

struct vfsmount *mnt_clone_internal(const struct path *path)
{
	struct mount *p;
	p = clone_mnt(real_mount(path->mnt), path->dentry, CL_PRIVATE);
	if (IS_ERR(p))
		return ERR_CAST(p);
	p->mnt.mnt_flags |= MNT_INTERNAL;
	return &p->mnt;
}

/*
 * Returns the mount which either has the specified mnt_id, or has the next
 * smallest id afer the specified one.
 */
static struct mount *mnt_find_id_at(struct mnt_namespace *ns, u64 mnt_id)
{
	struct rb_node *node = ns->mounts.rb_node;
	struct mount *ret = NULL;

	while (node) {
		struct mount *m = node_to_mount(node);

		if (mnt_id <= m->mnt_id_unique) {
			ret = node_to_mount(node);
			if (mnt_id == m->mnt_id_unique)
				break;
			node = node->rb_left;
		} else {
			node = node->rb_right;
		}
	}
	return ret;
}

/*
 * Returns the mount which either has the specified mnt_id, or has the next
 * greater id before the specified one.
 */
static struct mount *mnt_find_id_at_reverse(struct mnt_namespace *ns, u64 mnt_id)
{
	struct rb_node *node = ns->mounts.rb_node;
	struct mount *ret = NULL;

	while (node) {
		struct mount *m = node_to_mount(node);

		if (mnt_id >= m->mnt_id_unique) {
			ret = node_to_mount(node);
			if (mnt_id == m->mnt_id_unique)
				break;
			node = node->rb_right;
		} else {
			node = node->rb_left;
		}
	}
	return ret;
}

#ifdef CONFIG_PROC_FS

/* iterator; we want it to have access to namespace_sem, thus here... */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct proc_mounts *p = m->private;

	down_read(&namespace_sem);

	return mnt_find_id_at(p->ns, *pos);
}

static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct mount *next = NULL, *mnt = v;
	struct rb_node *node = rb_next(&mnt->mnt_node);

	++*pos;
	if (node) {
		next = node_to_mount(node);
		*pos = next->mnt_id_unique;
	}
	return next;
}

static void m_stop(struct seq_file *m, void *v)
{
	up_read(&namespace_sem);
}

static int m_show(struct seq_file *m, void *v)
{
	struct proc_mounts *p = m->private;
	struct mount *r = v;
	return p->show(m, &r->mnt);
}

const struct seq_operations mounts_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= m_show,
};

#endif  /* CONFIG_PROC_FS */

/**
 * may_umount_tree - check if a mount tree is busy
 * @m: root of mount tree
 *
 * This is called to check if a tree of mounts has any
 * open files, pwds, chroots or sub mounts that are
 * busy.
 */
int may_umount_tree(struct vfsmount *m)
{
	struct mount *mnt = real_mount(m);
	bool busy = false;

	/* write lock needed for mnt_get_count */
	lock_mount_hash();
	for (struct mount *p = mnt; p; p = next_mnt(p, mnt)) {
		if (mnt_get_count(p) > (p == mnt ? 2 : 1)) {
			busy = true;
			break;
		}
	}
	unlock_mount_hash();

	return !busy;
}

EXPORT_SYMBOL(may_umount_tree);

/**
 * may_umount - check if a mount point is busy
 * @mnt: root of mount
 *
 * This is called to check if a mount point has any
 * open files, pwds, chroots or sub mounts. If the
 * mount has sub mounts this will return busy
 * regardless of whether the sub mounts are busy.
 *
 * Doesn't take quota and stuff into account. IOW, in some cases it will
 * give false negatives. The main reason why it's here is that we need
 * a non-destructive way to look for easily umountable filesystems.
 */
int may_umount(struct vfsmount *mnt)
{
	int ret = 1;
	down_read(&namespace_sem);
	lock_mount_hash();
	if (propagate_mount_busy(real_mount(mnt), 2))
		ret = 0;
	unlock_mount_hash();
	up_read(&namespace_sem);
	return ret;
}

EXPORT_SYMBOL(may_umount);

#ifdef CONFIG_FSNOTIFY
static void mnt_notify(struct mount *p)
{
	if (!p->prev_ns && p->mnt_ns) {
		fsnotify_mnt_attach(p->mnt_ns, &p->mnt);
	} else if (p->prev_ns && !p->mnt_ns) {
		fsnotify_mnt_detach(p->prev_ns, &p->mnt);
	} else if (p->prev_ns == p->mnt_ns) {
		fsnotify_mnt_move(p->mnt_ns, &p->mnt);
	} else {
		fsnotify_mnt_detach(p->prev_ns, &p->mnt);
		fsnotify_mnt_attach(p->mnt_ns, &p->mnt);
	}
	p->prev_ns = p->mnt_ns;
}

static void notify_mnt_list(void)
{
	struct mount *m, *tmp;
	/*
	 * Notify about mounts that were added/reparented/detached/remain
	 * connected after unmount.
	 */
	list_for_each_entry_safe(m, tmp, &notify_list, to_notify) {
		mnt_notify(m);
		list_del_init(&m->to_notify);
	}
}

static bool need_notify_mnt_list(void)
{
	return !list_empty(&notify_list);
}
#else
static void notify_mnt_list(void)
{
}

static bool need_notify_mnt_list(void)
{
	return false;
}
#endif

static void free_mnt_ns(struct mnt_namespace *);
static void namespace_unlock(void)
{
	struct hlist_head head;
	struct hlist_node *p;
	struct mount *m;
	struct mnt_namespace *ns = emptied_ns;
	LIST_HEAD(list);

	hlist_move_list(&unmounted, &head);
	list_splice_init(&ex_mountpoints, &list);
	emptied_ns = NULL;

	if (need_notify_mnt_list()) {
		/*
		 * No point blocking out concurrent readers while notifications
		 * are sent. This will also allow statmount()/listmount() to run
		 * concurrently.
		 */
		downgrade_write(&namespace_sem);
		notify_mnt_list();
		up_read(&namespace_sem);
	} else {
		up_write(&namespace_sem);
	}
	if (unlikely(ns)) {
		/* Make sure we notice when we leak mounts. */
		VFS_WARN_ON_ONCE(!mnt_ns_empty(ns));
		free_mnt_ns(ns);
	}

	shrink_dentry_list(&list);

	if (likely(hlist_empty(&head)))
		return;

	synchronize_rcu_expedited();

	hlist_for_each_entry_safe(m, p, &head, mnt_umount) {
		hlist_del(&m->mnt_umount);
		mntput(&m->mnt);
	}
}

static inline void namespace_lock(void)
{
	down_write(&namespace_sem);
}

DEFINE_GUARD(namespace_lock, struct rw_semaphore *, namespace_lock(), namespace_unlock())

enum umount_tree_flags {
	UMOUNT_SYNC = 1,
	UMOUNT_PROPAGATE = 2,
	UMOUNT_CONNECTED = 4,
};

static bool disconnect_mount(struct mount *mnt, enum umount_tree_flags how)
{
	/* Leaving mounts connected is only valid for lazy umounts */
	if (how & UMOUNT_SYNC)
		return true;

	/* A mount without a parent has nothing to be connected to */
	if (!mnt_has_parent(mnt))
		return true;

	/* Because the reference counting rules change when mounts are
	 * unmounted and connected, umounted mounts may not be
	 * connected to mounted mounts.
	 */
	if (!(mnt->mnt_parent->mnt.mnt_flags & MNT_UMOUNT))
		return true;

	/* Has it been requested that the mount remain connected? */
	if (how & UMOUNT_CONNECTED)
		return false;

	/* Is the mount locked such that it needs to remain connected? */
	if (IS_MNT_LOCKED(mnt))
		return false;

	/* By default disconnect the mount */
	return true;
}

/*
 * mount_lock must be held
 * namespace_sem must be held for write
 */
static void umount_tree(struct mount *mnt, enum umount_tree_flags how)
{
	LIST_HEAD(tmp_list);
	struct mount *p;

	if (how & UMOUNT_PROPAGATE)
		propagate_mount_unlock(mnt);

	/* Gather the mounts to umount */
	for (p = mnt; p; p = next_mnt(p, mnt)) {
		p->mnt.mnt_flags |= MNT_UMOUNT;
		if (mnt_ns_attached(p))
			move_from_ns(p);
		list_add_tail(&p->mnt_list, &tmp_list);
	}

	/* Hide the mounts from mnt_mounts */
	list_for_each_entry(p, &tmp_list, mnt_list) {
		list_del_init(&p->mnt_child);
	}

	/* Add propagated mounts to the tmp_list */
	if (how & UMOUNT_PROPAGATE)
		propagate_umount(&tmp_list);

	while (!list_empty(&tmp_list)) {
		struct mnt_namespace *ns;
		bool disconnect;
		p = list_first_entry(&tmp_list, struct mount, mnt_list);
		list_del_init(&p->mnt_expire);
		list_del_init(&p->mnt_list);
		ns = p->mnt_ns;
		if (ns) {
			ns->nr_mounts--;
			__touch_mnt_namespace(ns);
		}
		p->mnt_ns = NULL;
		if (how & UMOUNT_SYNC)
			p->mnt.mnt_flags |= MNT_SYNC_UMOUNT;

		disconnect = disconnect_mount(p, how);
		if (mnt_has_parent(p)) {
			if (!disconnect) {
				/* Don't forget about p */
				list_add_tail(&p->mnt_child, &p->mnt_parent->mnt_mounts);
			} else {
				umount_mnt(p);
			}
		}
		change_mnt_propagation(p, MS_PRIVATE);
		if (disconnect)
			hlist_add_head(&p->mnt_umount, &unmounted);

		/*
		 * At this point p->mnt_ns is NULL, notification will be queued
		 * only if
		 *
		 *  - p->prev_ns is non-NULL *and*
		 *  - p->prev_ns->n_fsnotify_marks is non-NULL
		 *
		 * This will preclude queuing the mount if this is a cleanup
		 * after a failed copy_tree() or destruction of an anonymous
		 * namespace, etc.
		 */
		mnt_notify_add(p);
	}
}

static void shrink_submounts(struct mount *mnt);

static int do_umount_root(struct super_block *sb)
{
	int ret = 0;

	down_write(&sb->s_umount);
	if (!sb_rdonly(sb)) {
		struct fs_context *fc;

		fc = fs_context_for_reconfigure(sb->s_root, SB_RDONLY,
						SB_RDONLY);
		if (IS_ERR(fc)) {
			ret = PTR_ERR(fc);
		} else {
			ret = parse_monolithic_mount_data(fc, NULL);
			if (!ret)
				ret = reconfigure_super(fc);
			put_fs_context(fc);
		}
	}
	up_write(&sb->s_umount);
	return ret;
}

static int do_umount(struct mount *mnt, int flags)
{
	struct super_block *sb = mnt->mnt.mnt_sb;
	int retval;

	retval = security_sb_umount(&mnt->mnt, flags);
	if (retval)
		return retval;

	/*
	 * Allow userspace to request a mountpoint be expired rather than
	 * unmounting unconditionally. Unmount only happens if:
	 *  (1) the mark is already set (the mark is cleared by mntput())
	 *  (2) the usage count == 1 [parent vfsmount] + 1 [sys_umount]
	 */
	if (flags & MNT_EXPIRE) {
		if (&mnt->mnt == current->fs->root.mnt ||
		    flags & (MNT_FORCE | MNT_DETACH))
			return -EINVAL;

		/*
		 * probably don't strictly need the lock here if we examined
		 * all race cases, but it's a slowpath.
		 */
		lock_mount_hash();
		if (!list_empty(&mnt->mnt_mounts) || mnt_get_count(mnt) != 2) {
			unlock_mount_hash();
			return -EBUSY;
		}
		unlock_mount_hash();

		if (!xchg(&mnt->mnt_expiry_mark, 1))
			return -EAGAIN;
	}

	/*
	 * If we may have to abort operations to get out of this
	 * mount, and they will themselves hold resources we must
	 * allow the fs to do things. In the Unix tradition of
	 * 'Gee thats tricky lets do it in userspace' the umount_begin
	 * might fail to complete on the first run through as other tasks
	 * must return, and the like. Thats for the mount program to worry
	 * about for the moment.
	 */

	if (flags & MNT_FORCE && sb->s_op->umount_begin) {
		sb->s_op->umount_begin(sb);
	}

	/*
	 * No sense to grab the lock for this test, but test itself looks
	 * somewhat bogus. Suggestions for better replacement?
	 * Ho-hum... In principle, we might treat that as umount + switch
	 * to rootfs. GC would eventually take care of the old vfsmount.
	 * Actually it makes sense, especially if rootfs would contain a
	 * /reboot - static binary that would close all descriptors and
	 * call reboot(9). Then init(8) could umount root and exec /reboot.
	 */
	if (&mnt->mnt == current->fs->root.mnt && !(flags & MNT_DETACH)) {
		/*
		 * Special case for "unmounting" root ...
		 * we just try to remount it readonly.
		 */
		if (!ns_capable(sb->s_user_ns, CAP_SYS_ADMIN))
			return -EPERM;
		return do_umount_root(sb);
	}

	namespace_lock();
	lock_mount_hash();

	/* Repeat the earlier racy checks, now that we are holding the locks */
	retval = -EINVAL;
	if (!check_mnt(mnt))
		goto out;

	if (mnt->mnt.mnt_flags & MNT_LOCKED)
		goto out;

	if (!mnt_has_parent(mnt)) /* not the absolute root */
		goto out;

	event++;
	if (flags & MNT_DETACH) {
		umount_tree(mnt, UMOUNT_PROPAGATE);
		retval = 0;
	} else {
		smp_mb(); // paired with __legitimize_mnt()
		shrink_submounts(mnt);
		retval = -EBUSY;
		if (!propagate_mount_busy(mnt, 2)) {
			umount_tree(mnt, UMOUNT_PROPAGATE|UMOUNT_SYNC);
			retval = 0;
		}
	}
out:
	unlock_mount_hash();
	namespace_unlock();
	return retval;
}

/*
 * __detach_mounts - lazily unmount all mounts on the specified dentry
 *
 * During unlink, rmdir, and d_drop it is possible to loose the path
 * to an existing mountpoint, and wind up leaking the mount.
 * detach_mounts allows lazily unmounting those mounts instead of
 * leaking them.
 *
 * The caller may hold dentry->d_inode->i_rwsem.
 */
void __detach_mounts(struct dentry *dentry)
{
	struct pinned_mountpoint mp = {};
	struct mount *mnt;

	namespace_lock();
	lock_mount_hash();
	if (!lookup_mountpoint(dentry, &mp))
		goto out_unlock;

	event++;
	while (mp.node.next) {
		mnt = hlist_entry(mp.node.next, struct mount, mnt_mp_list);
		if (mnt->mnt.mnt_flags & MNT_UMOUNT) {
			umount_mnt(mnt);
			hlist_add_head(&mnt->mnt_umount, &unmounted);
		}
		else umount_tree(mnt, UMOUNT_CONNECTED);
	}
	unpin_mountpoint(&mp);
out_unlock:
	unlock_mount_hash();
	namespace_unlock();
}

/*
 * Is the caller allowed to modify his namespace?
 */
bool may_mount(void)
{
	return ns_capable(current->nsproxy->mnt_ns->user_ns, CAP_SYS_ADMIN);
}

static void warn_mandlock(void)
{
	pr_warn_once("=======================================================\n"
		     "WARNING: The mand mount option has been deprecated and\n"
		     "         and is ignored by this kernel. Remove the mand\n"
		     "         option from the mount to silence this warning.\n"
		     "=======================================================\n");
}

static int can_umount(const struct path *path, int flags)
{
	struct mount *mnt = real_mount(path->mnt);
	struct super_block *sb = path->dentry->d_sb;

	if (!may_mount())
		return -EPERM;
	if (!path_mounted(path))
		return -EINVAL;
	if (!check_mnt(mnt))
		return -EINVAL;
	if (mnt->mnt.mnt_flags & MNT_LOCKED) /* Check optimistically */
		return -EINVAL;
	if (flags & MNT_FORCE && !ns_capable(sb->s_user_ns, CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

// caller is responsible for flags being sane
int path_umount(struct path *path, int flags)
{
	struct mount *mnt = real_mount(path->mnt);
	int ret;

	ret = can_umount(path, flags);
	if (!ret)
		ret = do_umount(mnt, flags);

	/* we mustn't call path_put() as that would clear mnt_expiry_mark */
	dput(path->dentry);
	mntput_no_expire(mnt);
	return ret;
}

static int ksys_umount(char __user *name, int flags)
{
	int lookup_flags = LOOKUP_MOUNTPOINT;
	struct path path;
	int ret;

	// basic validity checks done first
	if (flags & ~(MNT_FORCE | MNT_DETACH | MNT_EXPIRE | UMOUNT_NOFOLLOW))
		return -EINVAL;

	if (!(flags & UMOUNT_NOFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;
	ret = user_path_at(AT_FDCWD, name, lookup_flags, &path);
	if (ret)
		return ret;
	return path_umount(&path, flags);
}

SYSCALL_DEFINE2(umount, char __user *, name, int, flags)
{
	return ksys_umount(name, flags);
}

#ifdef __ARCH_WANT_SYS_OLDUMOUNT

/*
 *	The 2.0 compatible umount. No flags.
 */
SYSCALL_DEFINE1(oldumount, char __user *, name)
{
	return ksys_umount(name, 0);
}

#endif

static bool is_mnt_ns_file(struct dentry *dentry)
{
	struct ns_common *ns;

	/* Is this a proxy for a mount namespace? */
	if (dentry->d_op != &ns_dentry_operations)
		return false;

	ns = d_inode(dentry)->i_private;

	return ns->ops == &mntns_operations;
}

struct ns_common *from_mnt_ns(struct mnt_namespace *mnt)
{
	return &mnt->ns;
}

struct mnt_namespace *get_sequential_mnt_ns(struct mnt_namespace *mntns, bool previous)
{
	guard(rcu)();

	for (;;) {
		struct list_head *list;

		if (previous)
			list = rcu_dereference(list_bidir_prev_rcu(&mntns->mnt_ns_list));
		else
			list = rcu_dereference(list_next_rcu(&mntns->mnt_ns_list));
		if (list_is_head(list, &mnt_ns_list))
			return ERR_PTR(-ENOENT);

		mntns = list_entry_rcu(list, struct mnt_namespace, mnt_ns_list);

		/*
		 * The last passive reference count is put with RCU
		 * delay so accessing the mount namespace is not just
		 * safe but all relevant members are still valid.
		 */
		if (!ns_capable_noaudit(mntns->user_ns, CAP_SYS_ADMIN))
			continue;

		/*
		 * We need an active reference count as we're persisting
		 * the mount namespace and it might already be on its
		 * deathbed.
		 */
		if (!refcount_inc_not_zero(&mntns->ns.count))
			continue;

		return mntns;
	}
}

struct mnt_namespace *mnt_ns_from_dentry(struct dentry *dentry)
{
	if (!is_mnt_ns_file(dentry))
		return NULL;

	return to_mnt_ns(get_proc_ns(dentry->d_inode));
}

static bool mnt_ns_loop(struct dentry *dentry)
{
	/* Could bind mounting the mount namespace inode cause a
	 * mount namespace loop?
	 */
	struct mnt_namespace *mnt_ns = mnt_ns_from_dentry(dentry);

	if (!mnt_ns)
		return false;

	return current->nsproxy->mnt_ns->seq >= mnt_ns->seq;
}

struct mount *copy_tree(struct mount *src_root, struct dentry *dentry,
					int flag)
{
	struct mount *res, *src_parent, *src_root_child, *src_mnt,
		*dst_parent, *dst_mnt;

	if (!(flag & CL_COPY_UNBINDABLE) && IS_MNT_UNBINDABLE(src_root))
		return ERR_PTR(-EINVAL);

	if (!(flag & CL_COPY_MNT_NS_FILE) && is_mnt_ns_file(dentry))
		return ERR_PTR(-EINVAL);

	res = dst_mnt = clone_mnt(src_root, dentry, flag);
	if (IS_ERR(dst_mnt))
		return dst_mnt;

	src_parent = src_root;

	list_for_each_entry(src_root_child, &src_root->mnt_mounts, mnt_child) {
		if (!is_subdir(src_root_child->mnt_mountpoint, dentry))
			continue;

		for (src_mnt = src_root_child; src_mnt;
		    src_mnt = next_mnt(src_mnt, src_root_child)) {
			if (!(flag & CL_COPY_UNBINDABLE) &&
			    IS_MNT_UNBINDABLE(src_mnt)) {
				if (src_mnt->mnt.mnt_flags & MNT_LOCKED) {
					/* Both unbindable and locked. */
					dst_mnt = ERR_PTR(-EPERM);
					goto out;
				} else {
					src_mnt = skip_mnt_tree(src_mnt);
					continue;
				}
			}
			if (!(flag & CL_COPY_MNT_NS_FILE) &&
			    is_mnt_ns_file(src_mnt->mnt.mnt_root)) {
				src_mnt = skip_mnt_tree(src_mnt);
				continue;
			}
			while (src_parent != src_mnt->mnt_parent) {
				src_parent = src_parent->mnt_parent;
				dst_mnt = dst_mnt->mnt_parent;
			}

			src_parent = src_mnt;
			dst_parent = dst_mnt;
			dst_mnt = clone_mnt(src_mnt, src_mnt->mnt.mnt_root, flag);
			if (IS_ERR(dst_mnt))
				goto out;
			lock_mount_hash();
			if (src_mnt->mnt.mnt_flags & MNT_LOCKED)
				dst_mnt->mnt.mnt_flags |= MNT_LOCKED;
			if (unlikely(flag & CL_EXPIRE)) {
				/* stick the duplicate mount on the same expiry
				 * list as the original if that was on one */
				if (!list_empty(&src_mnt->mnt_expire))
					list_add(&dst_mnt->mnt_expire,
						 &src_mnt->mnt_expire);
			}
			attach_mnt(dst_mnt, dst_parent, src_parent->mnt_mp);
			unlock_mount_hash();
		}
	}
	return res;

out:
	if (res) {
		lock_mount_hash();
		umount_tree(res, UMOUNT_SYNC);
		unlock_mount_hash();
	}
	return dst_mnt;
}

static inline bool extend_array(struct path **res, struct path **to_free,
				unsigned n, unsigned *count, unsigned new_count)
{
	struct path *p;

	if (likely(n < *count))
		return true;
	p = kmalloc_array(new_count, sizeof(struct path), GFP_KERNEL);
	if (p && *count)
		memcpy(p, *res, *count * sizeof(struct path));
	*count = new_count;
	kfree(*to_free);
	*to_free = *res = p;
	return p;
}

struct path *collect_paths(const struct path *path,
			      struct path *prealloc, unsigned count)
{
	struct mount *root = real_mount(path->mnt);
	struct mount *child;
	struct path *res = prealloc, *to_free = NULL;
	unsigned n = 0;

	guard(rwsem_read)(&namespace_sem);

	if (!check_mnt(root))
		return ERR_PTR(-EINVAL);
	if (!extend_array(&res, &to_free, 0, &count, 32))
		return ERR_PTR(-ENOMEM);
	res[n++] = *path;
	list_for_each_entry(child, &root->mnt_mounts, mnt_child) {
		if (!is_subdir(child->mnt_mountpoint, path->dentry))
			continue;
		for (struct mount *m = child; m; m = next_mnt(m, child)) {
			if (!extend_array(&res, &to_free, n, &count, 2 * count))
				return ERR_PTR(-ENOMEM);
			res[n].mnt = &m->mnt;
			res[n].dentry = m->mnt.mnt_root;
			n++;
		}
	}
	if (!extend_array(&res, &to_free, n, &count, count + 1))
		return ERR_PTR(-ENOMEM);
	memset(res + n, 0, (count - n) * sizeof(struct path));
	for (struct path *p = res; p->mnt; p++)
		path_get(p);
	return res;
}

void drop_collected_paths(struct path *paths, struct path *prealloc)
{
	for (struct path *p = paths; p->mnt; p++)
		path_put(p);
	if (paths != prealloc)
		kfree(paths);
}

static struct mnt_namespace *alloc_mnt_ns(struct user_namespace *, bool);

void dissolve_on_fput(struct vfsmount *mnt)
{
	struct mount *m = real_mount(mnt);

	/*
	 * m used to be the root of anon namespace; if it still is one,
	 * we need to dissolve the mount tree and free that namespace.
	 * Let's try to avoid taking namespace_sem if we can determine
	 * that there's nothing to do without it - rcu_read_lock() is
	 * enough to make anon_ns_root() memory-safe and once m has
	 * left its namespace, it's no longer our concern, since it will
	 * never become a root of anon ns again.
	 */

	scoped_guard(rcu) {
		if (!anon_ns_root(m))
			return;
	}

	scoped_guard(namespace_lock, &namespace_sem) {
		if (!anon_ns_root(m))
			return;

		emptied_ns = m->mnt_ns;
		lock_mount_hash();
		umount_tree(m, UMOUNT_CONNECTED);
		unlock_mount_hash();
	}
}

static bool __has_locked_children(struct mount *mnt, struct dentry *dentry)
{
	struct mount *child;

	list_for_each_entry(child, &mnt->mnt_mounts, mnt_child) {
		if (!is_subdir(child->mnt_mountpoint, dentry))
			continue;

		if (child->mnt.mnt_flags & MNT_LOCKED)
			return true;
	}
	return false;
}

bool has_locked_children(struct mount *mnt, struct dentry *dentry)
{
	bool res;

	read_seqlock_excl(&mount_lock);
	res = __has_locked_children(mnt, dentry);
	read_sequnlock_excl(&mount_lock);
	return res;
}

/*
 * Check that there aren't references to earlier/same mount namespaces in the
 * specified subtree.  Such references can act as pins for mount namespaces
 * that aren't checked by the mount-cycle checking code, thereby allowing
 * cycles to be made.
 */
static bool check_for_nsfs_mounts(struct mount *subtree)
{
	struct mount *p;
	bool ret = false;

	lock_mount_hash();
	for (p = subtree; p; p = next_mnt(p, subtree))
		if (mnt_ns_loop(p->mnt.mnt_root))
			goto out;

	ret = true;
out:
	unlock_mount_hash();
	return ret;
}

/**
 * clone_private_mount - create a private clone of a path
 * @path: path to clone
 *
 * This creates a new vfsmount, which will be the clone of @path.  The new mount
 * will not be attached anywhere in the namespace and will be private (i.e.
 * changes to the originating mount won't be propagated into this).
 *
 * This assumes caller has called or done the equivalent of may_mount().
 *
 * Release with mntput().
 */
struct vfsmount *clone_private_mount(const struct path *path)
{
	struct mount *old_mnt = real_mount(path->mnt);
	struct mount *new_mnt;

	guard(rwsem_read)(&namespace_sem);

	if (IS_MNT_UNBINDABLE(old_mnt))
		return ERR_PTR(-EINVAL);

	/*
	 * Make sure the source mount is acceptable.
	 * Anything mounted in our mount namespace is allowed.
	 * Otherwise, it must be the root of an anonymous mount
	 * namespace, and we need to make sure no namespace
	 * loops get created.
	 */
	if (!check_mnt(old_mnt)) {
		if (!anon_ns_root(old_mnt))
			return ERR_PTR(-EINVAL);

		if (!check_for_nsfs_mounts(old_mnt))
			return ERR_PTR(-EINVAL);
	}

        if (!ns_capable(old_mnt->mnt_ns->user_ns, CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);

	if (__has_locked_children(old_mnt, path->dentry))
		return ERR_PTR(-EINVAL);

	new_mnt = clone_mnt(old_mnt, path->dentry, CL_PRIVATE);
	if (IS_ERR(new_mnt))
		return ERR_PTR(-EINVAL);

	/* Longterm mount to be removed by kern_unmount*() */
	new_mnt->mnt_ns = MNT_NS_INTERNAL;
	return &new_mnt->mnt;
}
EXPORT_SYMBOL_GPL(clone_private_mount);

static void lock_mnt_tree(struct mount *mnt)
{
	struct mount *p;

	for (p = mnt; p; p = next_mnt(p, mnt)) {
		int flags = p->mnt.mnt_flags;
		/* Don't allow unprivileged users to change mount flags */
		flags |= MNT_LOCK_ATIME;

		if (flags & MNT_READONLY)
			flags |= MNT_LOCK_READONLY;

		if (flags & MNT_NODEV)
			flags |= MNT_LOCK_NODEV;

		if (flags & MNT_NOSUID)
			flags |= MNT_LOCK_NOSUID;

		if (flags & MNT_NOEXEC)
			flags |= MNT_LOCK_NOEXEC;
		/* Don't allow unprivileged users to reveal what is under a mount */
		if (list_empty(&p->mnt_expire) && p != mnt)
			flags |= MNT_LOCKED;
		p->mnt.mnt_flags = flags;
	}
}

static void cleanup_group_ids(struct mount *mnt, struct mount *end)
{
	struct mount *p;

	for (p = mnt; p != end; p = next_mnt(p, mnt)) {
		if (p->mnt_group_id && !IS_MNT_SHARED(p))
			mnt_release_group_id(p);
	}
}

static int invent_group_ids(struct mount *mnt, bool recurse)
{
	struct mount *p;

	for (p = mnt; p; p = recurse ? next_mnt(p, mnt) : NULL) {
		if (!p->mnt_group_id) {
			int err = mnt_alloc_group_id(p);
			if (err) {
				cleanup_group_ids(mnt, p);
				return err;
			}
		}
	}

	return 0;
}

int count_mounts(struct mnt_namespace *ns, struct mount *mnt)
{
	unsigned int max = READ_ONCE(sysctl_mount_max);
	unsigned int mounts = 0;
	struct mount *p;

	if (ns->nr_mounts >= max)
		return -ENOSPC;
	max -= ns->nr_mounts;
	if (ns->pending_mounts >= max)
		return -ENOSPC;
	max -= ns->pending_mounts;

	for (p = mnt; p; p = next_mnt(p, mnt))
		mounts++;

	if (mounts > max)
		return -ENOSPC;

	ns->pending_mounts += mounts;
	return 0;
}

enum mnt_tree_flags_t {
	MNT_TREE_BENEATH = BIT(0),
	MNT_TREE_PROPAGATION = BIT(1),
};

/**
 * attach_recursive_mnt - attach a source mount tree
 * @source_mnt: mount tree to be attached
 * @dest_mnt:   mount that @source_mnt will be mounted on
 * @dest_mp:    the mountpoint @source_mnt will be mounted at
 *
 *  NOTE: in the table below explains the semantics when a source mount
 *  of a given type is attached to a destination mount of a given type.
 * ---------------------------------------------------------------------------
 * |         BIND MOUNT OPERATION                                            |
 * |**************************************************************************
 * | source-->| shared        |       private  |       slave    | unbindable |
 * | dest     |               |                |                |            |
 * |   |      |               |                |                |            |
 * |   v      |               |                |                |            |
 * |**************************************************************************
 * |  shared  | shared (++)   |     shared (+) |     shared(+++)|  invalid   |
 * |          |               |                |                |            |
 * |non-shared| shared (+)    |      private   |      slave (*) |  invalid   |
 * ***************************************************************************
 * A bind operation clones the source mount and mounts the clone on the
 * destination mount.
 *
 * (++)  the cloned mount is propagated to all the mounts in the propagation
 * 	 tree of the destination mount and the cloned mount is added to
 * 	 the peer group of the source mount.
 * (+)   the cloned mount is created under the destination mount and is marked
 *       as shared. The cloned mount is added to the peer group of the source
 *       mount.
 * (+++) the mount is propagated to all the mounts in the propagation tree
 *       of the destination mount and the cloned mount is made slave
 *       of the same master as that of the source mount. The cloned mount
 *       is marked as 'shared and slave'.
 * (*)   the cloned mount is made a slave of the same master as that of the
 * 	 source mount.
 *
 * ---------------------------------------------------------------------------
 * |         		MOVE MOUNT OPERATION                                 |
 * |**************************************************************************
 * | source-->| shared        |       private  |       slave    | unbindable |
 * | dest     |               |                |                |            |
 * |   |      |               |                |                |            |
 * |   v      |               |                |                |            |
 * |**************************************************************************
 * |  shared  | shared (+)    |     shared (+) |    shared(+++) |  invalid   |
 * |          |               |                |                |            |
 * |non-shared| shared (+*)   |      private   |    slave (*)   | unbindable |
 * ***************************************************************************
 *
 * (+)  the mount is moved to the destination. And is then propagated to
 * 	all the mounts in the propagation tree of the destination mount.
 * (+*)  the mount is moved to the destination.
 * (+++)  the mount is moved to the destination and is then propagated to
 * 	all the mounts belonging to the destination mount's propagation tree.
 * 	the mount is marked as 'shared and slave'.
 * (*)	the mount continues to be a slave at the new location.
 *
 * if the source mount is a tree, the operations explained above is
 * applied to each mount in the tree.
 * Must be called without spinlocks held, since this function can sleep
 * in allocations.
 *
 * Context: The function expects namespace_lock() to be held.
 * Return: If @source_mnt was successfully attached 0 is returned.
 *         Otherwise a negative error code is returned.
 */
static int attach_recursive_mnt(struct mount *source_mnt,
				struct mount *dest_mnt,
				struct mountpoint *dest_mp)
{
	struct user_namespace *user_ns = current->nsproxy->mnt_ns->user_ns;
	HLIST_HEAD(tree_list);
	struct mnt_namespace *ns = dest_mnt->mnt_ns;
	struct pinned_mountpoint root = {};
	struct mountpoint *shorter = NULL;
	struct mount *child, *p;
	struct mount *top;
	struct hlist_node *n;
	int err = 0;
	bool moving = mnt_has_parent(source_mnt);

	/*
	 * Preallocate a mountpoint in case the new mounts need to be
	 * mounted beneath mounts on the same mountpoint.
	 */
	for (top = source_mnt; unlikely(top->overmount); top = top->overmount) {
		if (!shorter && is_mnt_ns_file(top->mnt.mnt_root))
			shorter = top->mnt_mp;
	}
	err = get_mountpoint(top->mnt.mnt_root, &root);
	if (err)
		return err;

	/* Is there space to add these mounts to the mount namespace? */
	if (!moving) {
		err = count_mounts(ns, source_mnt);
		if (err)
			goto out;
	}

	if (IS_MNT_SHARED(dest_mnt)) {
		err = invent_group_ids(source_mnt, true);
		if (err)
			goto out;
		err = propagate_mnt(dest_mnt, dest_mp, source_mnt, &tree_list);
	}
	lock_mount_hash();
	if (err)
		goto out_cleanup_ids;

	if (IS_MNT_SHARED(dest_mnt)) {
		for (p = source_mnt; p; p = next_mnt(p, source_mnt))
			set_mnt_shared(p);
	}

	if (moving) {
		umount_mnt(source_mnt);
		mnt_notify_add(source_mnt);
		/* if the mount is moved, it should no longer be expired
		 * automatically */
		list_del_init(&source_mnt->mnt_expire);
	} else {
		if (source_mnt->mnt_ns) {
			/* move from anon - the caller will destroy */
			emptied_ns = source_mnt->mnt_ns;
			for (p = source_mnt; p; p = next_mnt(p, source_mnt))
				move_from_ns(p);
		}
	}

	mnt_set_mountpoint(dest_mnt, dest_mp, source_mnt);
	/*
	 * Now the original copy is in the same state as the secondaries -
	 * its root attached to mountpoint, but not hashed and all mounts
	 * in it are either in our namespace or in no namespace at all.
	 * Add the original to the list of copies and deal with the
	 * rest of work for all of them uniformly.
	 */
	hlist_add_head(&source_mnt->mnt_hash, &tree_list);

	hlist_for_each_entry_safe(child, n, &tree_list, mnt_hash) {
		struct mount *q;
		hlist_del_init(&child->mnt_hash);
		/* Notice when we are propagating across user namespaces */
		if (child->mnt_parent->mnt_ns->user_ns != user_ns)
			lock_mnt_tree(child);
		q = __lookup_mnt(&child->mnt_parent->mnt,
				 child->mnt_mountpoint);
		if (q) {
			struct mountpoint *mp = root.mp;
			struct mount *r = child;
			while (unlikely(r->overmount))
				r = r->overmount;
			if (unlikely(shorter) && child != source_mnt)
				mp = shorter;
			mnt_change_mountpoint(r, mp, q);
		}
		commit_tree(child);
	}
	unpin_mountpoint(&root);
	unlock_mount_hash();

	return 0;

 out_cleanup_ids:
	while (!hlist_empty(&tree_list)) {
		child = hlist_entry(tree_list.first, struct mount, mnt_hash);
		child->mnt_parent->mnt_ns->pending_mounts = 0;
		umount_tree(child, UMOUNT_SYNC);
	}
	unlock_mount_hash();
	cleanup_group_ids(source_mnt, NULL);
 out:
	ns->pending_mounts = 0;

	read_seqlock_excl(&mount_lock);
	unpin_mountpoint(&root);
	read_sequnlock_excl(&mount_lock);

	return err;
}

/**
 * do_lock_mount - lock mount and mountpoint
 * @path:    target path
 * @beneath: whether the intention is to mount beneath @path
 *
 * Follow the mount stack on @path until the top mount @mnt is found. If
 * the initial @path->{mnt,dentry} is a mountpoint lookup the first
 * mount stacked on top of it. Then simply follow @{mnt,mnt->mnt_root}
 * until nothing is stacked on top of it anymore.
 *
 * Acquire the inode_lock() on the top mount's ->mnt_root to protect
 * against concurrent removal of the new mountpoint from another mount
 * namespace.
 *
 * If @beneath is requested, acquire inode_lock() on @mnt's mountpoint
 * @mp on @mnt->mnt_parent must be acquired. This protects against a
 * concurrent unlink of @mp->mnt_dentry from another mount namespace
 * where @mnt doesn't have a child mount mounted @mp. A concurrent
 * removal of @mnt->mnt_root doesn't matter as nothing will be mounted
 * on top of it for @beneath.
 *
 * In addition, @beneath needs to make sure that @mnt hasn't been
 * unmounted or moved from its current mountpoint in between dropping
 * @mount_lock and acquiring @namespace_sem. For the !@beneath case @mnt
 * being unmounted would be detected later by e.g., calling
 * check_mnt(mnt) in the function it's called from. For the @beneath
 * case however, it's useful to detect it directly in do_lock_mount().
 * If @mnt hasn't been unmounted then @mnt->mnt_mountpoint still points
 * to @mnt->mnt_mp->m_dentry. But if @mnt has been unmounted it will
 * point to @mnt->mnt_root and @mnt->mnt_mp will be NULL.
 *
 * Return: Either the target mountpoint on the top mount or the top
 *         mount's mountpoint.
 */
static int do_lock_mount(struct path *path, struct pinned_mountpoint *pinned, bool beneath)
{
	struct vfsmount *mnt = path->mnt;
	struct dentry *dentry;
	struct path under = {};
	int err = -ENOENT;

	for (;;) {
		struct mount *m = real_mount(mnt);

		if (beneath) {
			path_put(&under);
			read_seqlock_excl(&mount_lock);
			under.mnt = mntget(&m->mnt_parent->mnt);
			under.dentry = dget(m->mnt_mountpoint);
			read_sequnlock_excl(&mount_lock);
			dentry = under.dentry;
		} else {
			dentry = path->dentry;
		}

		inode_lock(dentry->d_inode);
		namespace_lock();

		if (unlikely(cant_mount(dentry) || !is_mounted(mnt)))
			break;		// not to be mounted on

		if (beneath && unlikely(m->mnt_mountpoint != dentry ||
				        &m->mnt_parent->mnt != under.mnt)) {
			namespace_unlock();
			inode_unlock(dentry->d_inode);
			continue;	// got moved
		}

		mnt = lookup_mnt(path);
		if (unlikely(mnt)) {
			namespace_unlock();
			inode_unlock(dentry->d_inode);
			path_put(path);
			path->mnt = mnt;
			path->dentry = dget(mnt->mnt_root);
			continue;	// got overmounted
		}
		err = get_mountpoint(dentry, pinned);
		if (err)
			break;
		if (beneath) {
			/*
			 * @under duplicates the references that will stay
			 * at least until namespace_unlock(), so the path_put()
			 * below is safe (and OK to do under namespace_lock -
			 * we are not dropping the final references here).
			 */
			path_put(&under);
		}
		return 0;
	}
	namespace_unlock();
	inode_unlock(dentry->d_inode);
	if (beneath)
		path_put(&under);
	return err;
}

static inline int lock_mount(struct path *path, struct pinned_mountpoint *m)
{
	return do_lock_mount(path, m, false);
}

static void unlock_mount(struct pinned_mountpoint *m)
{
	inode_unlock(m->mp->m_dentry->d_inode);
	read_seqlock_excl(&mount_lock);
	unpin_mountpoint(m);
	read_sequnlock_excl(&mount_lock);
	namespace_unlock();
}

static int graft_tree(struct mount *mnt, struct mount *p, struct mountpoint *mp)
{
	if (mnt->mnt.mnt_sb->s_flags & SB_NOUSER)
		return -EINVAL;

	if (d_is_dir(mp->m_dentry) !=
	      d_is_dir(mnt->mnt.mnt_root))
		return -ENOTDIR;

	return attach_recursive_mnt(mnt, p, mp);
}

/*
 * Sanity check the flags to change_mnt_propagation.
 */

static int flags_to_propagation_type(int ms_flags)
{
	int type = ms_flags & ~(MS_REC | MS_SILENT);

	/* Fail if any non-propagation flags are set */
	if (type & ~(MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE))
		return 0;
	/* Only one propagation flag should be set */
	if (!is_power_of_2(type))
		return 0;
	return type;
}

/*
 * recursively change the type of the mountpoint.
 */
static int do_change_type(struct path *path, int ms_flags)
{
	struct mount *m;
	struct mount *mnt = real_mount(path->mnt);
	int recurse = ms_flags & MS_REC;
	int type;
	int err = 0;

	if (!path_mounted(path))
		return -EINVAL;

	type = flags_to_propagation_type(ms_flags);
	if (!type)
		return -EINVAL;

	namespace_lock();
	if (!check_mnt(mnt)) {
		err = -EINVAL;
		goto out_unlock;
	}
	if (type == MS_SHARED) {
		err = invent_group_ids(mnt, recurse);
		if (err)
			goto out_unlock;
	}

	for (m = mnt; m; m = (recurse ? next_mnt(m, mnt) : NULL))
		change_mnt_propagation(m, type);

 out_unlock:
	namespace_unlock();
	return err;
}

/* may_copy_tree() - check if a mount tree can be copied
 * @path: path to the mount tree to be copied
 *
 * This helper checks if the caller may copy the mount tree starting
 * from @path->mnt. The caller may copy the mount tree under the
 * following circumstances:
 *
 * (1) The caller is located in the mount namespace of the mount tree.
 *     This also implies that the mount does not belong to an anonymous
 *     mount namespace.
 * (2) The caller tries to copy an nfs mount referring to a mount
 *     namespace, i.e., the caller is trying to copy a mount namespace
 *     entry from nsfs.
 * (3) The caller tries to copy a pidfs mount referring to a pidfd.
 * (4) The caller is trying to copy a mount tree that belongs to an
 *     anonymous mount namespace.
 *
 *     For that to be safe, this helper enforces that the origin mount
 *     namespace the anonymous mount namespace was created from is the
 *     same as the caller's mount namespace by comparing the sequence
 *     numbers.
 *
 *     This is not strictly necessary. The current semantics of the new
 *     mount api enforce that the caller must be located in the same
 *     mount namespace as the mount tree it interacts with. Using the
 *     origin sequence number preserves these semantics even for
 *     anonymous mount namespaces. However, one could envision extending
 *     the api to directly operate across mount namespace if needed.
 *
 *     The ownership of a non-anonymous mount namespace such as the
 *     caller's cannot change.
 *     => We know that the caller's mount namespace is stable.
 *
 *     If the origin sequence number of the anonymous mount namespace is
 *     the same as the sequence number of the caller's mount namespace.
 *     => The owning namespaces are the same.
 *
 *     ==> The earlier capability check on the owning namespace of the
 *         caller's mount namespace ensures that the caller has the
 *         ability to copy the mount tree.
 *
 * Returns true if the mount tree can be copied, false otherwise.
 */
static inline bool may_copy_tree(struct path *path)
{
	struct mount *mnt = real_mount(path->mnt);
	const struct dentry_operations *d_op;

	if (check_mnt(mnt))
		return true;

	d_op = path->dentry->d_op;
	if (d_op == &ns_dentry_operations)
		return true;

	if (d_op == &pidfs_dentry_operations)
		return true;

	if (!is_mounted(path->mnt))
		return false;

	return check_anonymous_mnt(mnt);
}


static struct mount *__do_loopback(struct path *old_path, int recurse)
{
	struct mount *old = real_mount(old_path->mnt);

	if (IS_MNT_UNBINDABLE(old))
		return ERR_PTR(-EINVAL);

	if (!may_copy_tree(old_path))
		return ERR_PTR(-EINVAL);

	if (!recurse && __has_locked_children(old, old_path->dentry))
		return ERR_PTR(-EINVAL);

	if (recurse)
		return copy_tree(old, old_path->dentry, CL_COPY_MNT_NS_FILE);
	else
		return clone_mnt(old, old_path->dentry, 0);
}

/*
 * do loopback mount.
 */
static int do_loopback(struct path *path, const char *old_name,
				int recurse)
{
	struct path old_path;
	struct mount *mnt = NULL, *parent;
	struct pinned_mountpoint mp = {};
	int err;
	if (!old_name || !*old_name)
		return -EINVAL;
	err = kern_path(old_name, LOOKUP_FOLLOW|LOOKUP_AUTOMOUNT, &old_path);
	if (err)
		return err;

	err = -EINVAL;
	if (mnt_ns_loop(old_path.dentry))
		goto out;

	err = lock_mount(path, &mp);
	if (err)
		goto out;

	parent = real_mount(path->mnt);
	if (!check_mnt(parent))
		goto out2;

	mnt = __do_loopback(&old_path, recurse);
	if (IS_ERR(mnt)) {
		err = PTR_ERR(mnt);
		goto out2;
	}

	err = graft_tree(mnt, parent, mp.mp);
	if (err) {
		lock_mount_hash();
		umount_tree(mnt, UMOUNT_SYNC);
		unlock_mount_hash();
	}
out2:
	unlock_mount(&mp);
out:
	path_put(&old_path);
	return err;
}

static struct file *open_detached_copy(struct path *path, bool recursive)
{
	struct mnt_namespace *ns, *mnt_ns = current->nsproxy->mnt_ns, *src_mnt_ns;
	struct user_namespace *user_ns = mnt_ns->user_ns;
	struct mount *mnt, *p;
	struct file *file;

	ns = alloc_mnt_ns(user_ns, true);
	if (IS_ERR(ns))
		return ERR_CAST(ns);

	namespace_lock();

	/*
	 * Record the sequence number of the source mount namespace.
	 * This needs to hold namespace_sem to ensure that the mount
	 * doesn't get attached.
	 */
	if (is_mounted(path->mnt)) {
		src_mnt_ns = real_mount(path->mnt)->mnt_ns;
		if (is_anon_ns(src_mnt_ns))
			ns->seq_origin = src_mnt_ns->seq_origin;
		else
			ns->seq_origin = src_mnt_ns->seq;
	}

	mnt = __do_loopback(path, recursive);
	if (IS_ERR(mnt)) {
		namespace_unlock();
		free_mnt_ns(ns);
		return ERR_CAST(mnt);
	}

	lock_mount_hash();
	for (p = mnt; p; p = next_mnt(p, mnt)) {
		mnt_add_to_ns(ns, p);
		ns->nr_mounts++;
	}
	ns->root = mnt;
	mntget(&mnt->mnt);
	unlock_mount_hash();
	namespace_unlock();

	mntput(path->mnt);
	path->mnt = &mnt->mnt;
	file = dentry_open(path, O_PATH, current_cred());
	if (IS_ERR(file))
		dissolve_on_fput(path->mnt);
	else
		file->f_mode |= FMODE_NEED_UNMOUNT;
	return file;
}

static struct file *vfs_open_tree(int dfd, const char __user *filename, unsigned int flags)
{
	int ret;
	struct path path __free(path_put) = {};
	int lookup_flags = LOOKUP_AUTOMOUNT | LOOKUP_FOLLOW;
	bool detached = flags & OPEN_TREE_CLONE;

	BUILD_BUG_ON(OPEN_TREE_CLOEXEC != O_CLOEXEC);

	if (flags & ~(AT_EMPTY_PATH | AT_NO_AUTOMOUNT | AT_RECURSIVE |
		      AT_SYMLINK_NOFOLLOW | OPEN_TREE_CLONE |
		      OPEN_TREE_CLOEXEC))
		return ERR_PTR(-EINVAL);

	if ((flags & (AT_RECURSIVE | OPEN_TREE_CLONE)) == AT_RECURSIVE)
		return ERR_PTR(-EINVAL);

	if (flags & AT_NO_AUTOMOUNT)
		lookup_flags &= ~LOOKUP_AUTOMOUNT;
	if (flags & AT_SYMLINK_NOFOLLOW)
		lookup_flags &= ~LOOKUP_FOLLOW;
	if (flags & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;

	if (detached && !may_mount())
		return ERR_PTR(-EPERM);

	ret = user_path_at(dfd, filename, lookup_flags, &path);
	if (unlikely(ret))
		return ERR_PTR(ret);

	if (detached)
		return open_detached_copy(&path, flags & AT_RECURSIVE);

	return dentry_open(&path, O_PATH, current_cred());
}

SYSCALL_DEFINE3(open_tree, int, dfd, const char __user *, filename, unsigned, flags)
{
	int fd;
	struct file *file __free(fput) = NULL;

	file = vfs_open_tree(dfd, filename, flags);
	if (IS_ERR(file))
		return PTR_ERR(file);

	fd = get_unused_fd_flags(flags & O_CLOEXEC);
	if (fd < 0)
		return fd;

	fd_install(fd, no_free_ptr(file));
	return fd;
}

/*
 * Don't allow locked mount flags to be cleared.
 *
 * No locks need to be held here while testing the various MNT_LOCK
 * flags because those flags can never be cleared once they are set.
 */
static bool can_change_locked_flags(struct mount *mnt, unsigned int mnt_flags)
{
	unsigned int fl = mnt->mnt.mnt_flags;

	if ((fl & MNT_LOCK_READONLY) &&
	    !(mnt_flags & MNT_READONLY))
		return false;

	if ((fl & MNT_LOCK_NODEV) &&
	    !(mnt_flags & MNT_NODEV))
		return false;

	if ((fl & MNT_LOCK_NOSUID) &&
	    !(mnt_flags & MNT_NOSUID))
		return false;

	if ((fl & MNT_LOCK_NOEXEC) &&
	    !(mnt_flags & MNT_NOEXEC))
		return false;

	if ((fl & MNT_LOCK_ATIME) &&
	    ((fl & MNT_ATIME_MASK) != (mnt_flags & MNT_ATIME_MASK)))
		return false;

	return true;
}

static int change_mount_ro_state(struct mount *mnt, unsigned int mnt_flags)
{
	bool readonly_request = (mnt_flags & MNT_READONLY);

	if (readonly_request == __mnt_is_readonly(&mnt->mnt))
		return 0;

	if (readonly_request)
		return mnt_make_readonly(mnt);

	mnt->mnt.mnt_flags &= ~MNT_READONLY;
	return 0;
}

static void set_mount_attributes(struct mount *mnt, unsigned int mnt_flags)
{
	mnt_flags |= mnt->mnt.mnt_flags & ~MNT_USER_SETTABLE_MASK;
	mnt->mnt.mnt_flags = mnt_flags;
	touch_mnt_namespace(mnt->mnt_ns);
}

static void mnt_warn_timestamp_expiry(struct path *mountpoint, struct vfsmount *mnt)
{
	struct super_block *sb = mnt->mnt_sb;

	if (!__mnt_is_readonly(mnt) &&
	   (!(sb->s_iflags & SB_I_TS_EXPIRY_WARNED)) &&
	   (ktime_get_real_seconds() + TIME_UPTIME_SEC_MAX > sb->s_time_max)) {
		char *buf, *mntpath;

		buf = (char *)__get_free_page(GFP_KERNEL);
		if (buf)
			mntpath = d_path(mountpoint, buf, PAGE_SIZE);
		else
			mntpath = ERR_PTR(-ENOMEM);
		if (IS_ERR(mntpath))
			mntpath = "(unknown)";

		pr_warn("%s filesystem being %s at %s supports timestamps until %ptTd (0x%llx)\n",
			sb->s_type->name,
			is_mounted(mnt) ? "remounted" : "mounted",
			mntpath, &sb->s_time_max,
			(unsigned long long)sb->s_time_max);

		sb->s_iflags |= SB_I_TS_EXPIRY_WARNED;
		if (buf)
			free_page((unsigned long)buf);
	}
}

/*
 * Handle reconfiguration of the mountpoint only without alteration of the
 * superblock it refers to.  This is triggered by specifying MS_REMOUNT|MS_BIND
 * to mount(2).
 */
static int do_reconfigure_mnt(struct path *path, unsigned int mnt_flags)
{
	struct super_block *sb = path->mnt->mnt_sb;
	struct mount *mnt = real_mount(path->mnt);
	int ret;

	if (!check_mnt(mnt))
		return -EINVAL;

	if (!path_mounted(path))
		return -EINVAL;

	if (!can_change_locked_flags(mnt, mnt_flags))
		return -EPERM;

	/*
	 * We're only checking whether the superblock is read-only not
	 * changing it, so only take down_read(&sb->s_umount).
	 */
	down_read(&sb->s_umount);
	lock_mount_hash();
	ret = change_mount_ro_state(mnt, mnt_flags);
	if (ret == 0)
		set_mount_attributes(mnt, mnt_flags);
	unlock_mount_hash();
	up_read(&sb->s_umount);

	mnt_warn_timestamp_expiry(path, &mnt->mnt);

	return ret;
}

/*
 * change filesystem flags. dir should be a physical root of filesystem.
 * If you've mounted a non-root directory somewhere and want to do remount
 * on it - tough luck.
 */
static int do_remount(struct path *path, int ms_flags, int sb_flags,
		      int mnt_flags, void *data)
{
	int err;
	struct super_block *sb = path->mnt->mnt_sb;
	struct mount *mnt = real_mount(path->mnt);
	struct fs_context *fc;

	if (!check_mnt(mnt))
		return -EINVAL;

	if (!path_mounted(path))
		return -EINVAL;

	if (!can_change_locked_flags(mnt, mnt_flags))
		return -EPERM;

	fc = fs_context_for_reconfigure(path->dentry, sb_flags, MS_RMT_MASK);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

	/*
	 * Indicate to the filesystem that the remount request is coming
	 * from the legacy mount system call.
	 */
	fc->oldapi = true;

	err = parse_monolithic_mount_data(fc, data);
	if (!err) {
		down_write(&sb->s_umount);
		err = -EPERM;
		if (ns_capable(sb->s_user_ns, CAP_SYS_ADMIN)) {
			err = reconfigure_super(fc);
			if (!err) {
				lock_mount_hash();
				set_mount_attributes(mnt, mnt_flags);
				unlock_mount_hash();
			}
		}
		up_write(&sb->s_umount);
	}

	mnt_warn_timestamp_expiry(path, &mnt->mnt);

	put_fs_context(fc);
	return err;
}

static inline int tree_contains_unbindable(struct mount *mnt)
{
	struct mount *p;
	for (p = mnt; p; p = next_mnt(p, mnt)) {
		if (IS_MNT_UNBINDABLE(p))
			return 1;
	}
	return 0;
}

static int do_set_group(struct path *from_path, struct path *to_path)
{
	struct mount *from, *to;
	int err;

	from = real_mount(from_path->mnt);
	to = real_mount(to_path->mnt);

	namespace_lock();

	err = -EINVAL;
	/* To and From must be mounted */
	if (!is_mounted(&from->mnt))
		goto out;
	if (!is_mounted(&to->mnt))
		goto out;

	err = -EPERM;
	/* We should be allowed to modify mount namespaces of both mounts */
	if (!ns_capable(from->mnt_ns->user_ns, CAP_SYS_ADMIN))
		goto out;
	if (!ns_capable(to->mnt_ns->user_ns, CAP_SYS_ADMIN))
		goto out;

	err = -EINVAL;
	/* To and From paths should be mount roots */
	if (!path_mounted(from_path))
		goto out;
	if (!path_mounted(to_path))
		goto out;

	/* Setting sharing groups is only allowed across same superblock */
	if (from->mnt.mnt_sb != to->mnt.mnt_sb)
		goto out;

	/* From mount root should be wider than To mount root */
	if (!is_subdir(to->mnt.mnt_root, from->mnt.mnt_root))
		goto out;

	/* From mount should not have locked children in place of To's root */
	if (__has_locked_children(from, to->mnt.mnt_root))
		goto out;

	/* Setting sharing groups is only allowed on private mounts */
	if (IS_MNT_SHARED(to) || IS_MNT_SLAVE(to))
		goto out;

	/* From should not be private */
	if (!IS_MNT_SHARED(from) && !IS_MNT_SLAVE(from))
		goto out;

	if (IS_MNT_SLAVE(from)) {
		hlist_add_behind(&to->mnt_slave, &from->mnt_slave);
		to->mnt_master = from->mnt_master;
	}

	if (IS_MNT_SHARED(from)) {
		to->mnt_group_id = from->mnt_group_id;
		list_add(&to->mnt_share, &from->mnt_share);
		set_mnt_shared(to);
	}

	err = 0;
out:
	namespace_unlock();
	return err;
}

/**
 * path_overmounted - check if path is overmounted
 * @path: path to check
 *
 * Check if path is overmounted, i.e., if there's a mount on top of
 * @path->mnt with @path->dentry as mountpoint.
 *
 * Context: namespace_sem must be held at least shared.
 * MUST NOT be called under lock_mount_hash() (there one should just
 * call __lookup_mnt() and check if it returns NULL).
 * Return: If path is overmounted true is returned, false if not.
 */
static inline bool path_overmounted(const struct path *path)
{
	unsigned seq = read_seqbegin(&mount_lock);
	bool no_child;

	rcu_read_lock();
	no_child = !__lookup_mnt(path->mnt, path->dentry);
	rcu_read_unlock();
	if (need_seqretry(&mount_lock, seq)) {
		read_seqlock_excl(&mount_lock);
		no_child = !__lookup_mnt(path->mnt, path->dentry);
		read_sequnlock_excl(&mount_lock);
	}
	return unlikely(!no_child);
}

/*
 * Check if there is a possibly empty chain of descent from p1 to p2.
 * Locks: namespace_sem (shared) or mount_lock (read_seqlock_excl).
 */
static bool mount_is_ancestor(const struct mount *p1, const struct mount *p2)
{
	while (p2 != p1 && mnt_has_parent(p2))
		p2 = p2->mnt_parent;
	return p2 == p1;
}

/**
 * can_move_mount_beneath - check that we can mount beneath the top mount
 * @from: mount to mount beneath
 * @to:   mount under which to mount
 * @mp:   mountpoint of @to
 *
 * - Make sure that @to->dentry is actually the root of a mount under
 *   which we can mount another mount.
 * - Make sure that nothing can be mounted beneath the caller's current
 *   root or the rootfs of the namespace.
 * - Make sure that the caller can unmount the topmost mount ensuring
 *   that the caller could reveal the underlying mountpoint.
 * - Ensure that nothing has been mounted on top of @from before we
 *   grabbed @namespace_sem to avoid creating pointless shadow mounts.
 * - Prevent mounting beneath a mount if the propagation relationship
 *   between the source mount, parent mount, and top mount would lead to
 *   nonsensical mount trees.
 *
 * Context: This function expects namespace_lock() to be held.
 * Return: On success 0, and on error a negative error code is returned.
 */
static int can_move_mount_beneath(const struct path *from,
				  const struct path *to,
				  const struct mountpoint *mp)
{
	struct mount *mnt_from = real_mount(from->mnt),
		     *mnt_to = real_mount(to->mnt),
		     *parent_mnt_to = mnt_to->mnt_parent;

	if (!mnt_has_parent(mnt_to))
		return -EINVAL;

	if (!path_mounted(to))
		return -EINVAL;

	if (IS_MNT_LOCKED(mnt_to))
		return -EINVAL;

	/* Avoid creating shadow mounts during mount propagation. */
	if (path_overmounted(from))
		return -EINVAL;

	/*
	 * Mounting beneath the rootfs only makes sense when the
	 * semantics of pivot_root(".", ".") are used.
	 */
	if (&mnt_to->mnt == current->fs->root.mnt)
		return -EINVAL;
	if (parent_mnt_to == current->nsproxy->mnt_ns->root)
		return -EINVAL;

	if (mount_is_ancestor(mnt_to, mnt_from))
		return -EINVAL;

	/*
	 * If the parent mount propagates to the child mount this would
	 * mean mounting @mnt_from on @mnt_to->mnt_parent and then
	 * propagating a copy @c of @mnt_from on top of @mnt_to. This
	 * defeats the whole purpose of mounting beneath another mount.
	 */
	if (propagation_would_overmount(parent_mnt_to, mnt_to, mp))
		return -EINVAL;

	/*
	 * If @mnt_to->mnt_parent propagates to @mnt_from this would
	 * mean propagating a copy @c of @mnt_from on top of @mnt_from.
	 * Afterwards @mnt_from would be mounted on top of
	 * @mnt_to->mnt_parent and @mnt_to would be unmounted from
	 * @mnt->mnt_parent and remounted on @mnt_from. But since @c is
	 * already mounted on @mnt_from, @mnt_to would ultimately be
	 * remounted on top of @c. Afterwards, @mnt_from would be
	 * covered by a copy @c of @mnt_from and @c would be covered by
	 * @mnt_from itself. This defeats the whole purpose of mounting
	 * @mnt_from beneath @mnt_to.
	 */
	if (check_mnt(mnt_from) &&
	    propagation_would_overmount(parent_mnt_to, mnt_from, mp))
		return -EINVAL;

	return 0;
}

/* may_use_mount() - check if a mount tree can be used
 * @mnt: vfsmount to be used
 *
 * This helper checks if the caller may use the mount tree starting
 * from @path->mnt. The caller may use the mount tree under the
 * following circumstances:
 *
 * (1) The caller is located in the mount namespace of the mount tree.
 *     This also implies that the mount does not belong to an anonymous
 *     mount namespace.
 * (2) The caller is trying to use a mount tree that belongs to an
 *     anonymous mount namespace.
 *
 *     For that to be safe, this helper enforces that the origin mount
 *     namespace the anonymous mount namespace was created from is the
 *     same as the caller's mount namespace by comparing the sequence
 *     numbers.
 *
 *     The ownership of a non-anonymous mount namespace such as the
 *     caller's cannot change.
 *     => We know that the caller's mount namespace is stable.
 *
 *     If the origin sequence number of the anonymous mount namespace is
 *     the same as the sequence number of the caller's mount namespace.
 *     => The owning namespaces are the same.
 *
 *     ==> The earlier capability check on the owning namespace of the
 *         caller's mount namespace ensures that the caller has the
 *         ability to use the mount tree.
 *
 * Returns true if the mount tree can be used, false otherwise.
 */
static inline bool may_use_mount(struct mount *mnt)
{
	if (check_mnt(mnt))
		return true;

	/*
	 * Make sure that noone unmounted the target path or somehow
	 * managed to get their hands on something purely kernel
	 * internal.
	 */
	if (!is_mounted(&mnt->mnt))
		return false;

	return check_anonymous_mnt(mnt);
}

static int do_move_mount(struct path *old_path,
			 struct path *new_path, enum mnt_tree_flags_t flags)
{
	struct mnt_namespace *ns;
	struct mount *p;
	struct mount *old;
	struct mount *parent;
	struct pinned_mountpoint mp;
	int err;
	bool beneath = flags & MNT_TREE_BENEATH;

	err = do_lock_mount(new_path, &mp, beneath);
	if (err)
		return err;

	old = real_mount(old_path->mnt);
	p = real_mount(new_path->mnt);
	parent = old->mnt_parent;
	ns = old->mnt_ns;

	err = -EINVAL;

	if (check_mnt(old)) {
		/* if the source is in our namespace... */
		/* ... it should be detachable from parent */
		if (!mnt_has_parent(old) || IS_MNT_LOCKED(old))
			goto out;
		/* ... and the target should be in our namespace */
		if (!check_mnt(p))
			goto out;
		/* parent of the source should not be shared */
		if (IS_MNT_SHARED(parent))
			goto out;
	} else {
		/*
		 * otherwise the source must be the root of some anon namespace.
		 */
		if (!anon_ns_root(old))
			goto out;
		/*
		 * Bail out early if the target is within the same namespace -
		 * subsequent checks would've rejected that, but they lose
		 * some corner cases if we check it early.
		 */
		if (ns == p->mnt_ns)
			goto out;
		/*
		 * Target should be either in our namespace or in an acceptable
		 * anon namespace, sensu check_anonymous_mnt().
		 */
		if (!may_use_mount(p))
			goto out;
	}

	if (!path_mounted(old_path))
		goto out;

	if (d_is_dir(new_path->dentry) !=
	    d_is_dir(old_path->dentry))
		goto out;

	if (beneath) {
		err = can_move_mount_beneath(old_path, new_path, mp.mp);
		if (err)
			goto out;

		err = -EINVAL;
		p = p->mnt_parent;
	}

	/*
	 * Don't move a mount tree containing unbindable mounts to a destination
	 * mount which is shared.
	 */
	if (IS_MNT_SHARED(p) && tree_contains_unbindable(old))
		goto out;
	err = -ELOOP;
	if (!check_for_nsfs_mounts(old))
		goto out;
	if (mount_is_ancestor(old, p))
		goto out;

	err = attach_recursive_mnt(old, p, mp.mp);
out:
	unlock_mount(&mp);
	return err;
}

static int do_move_mount_old(struct path *path, const char *old_name)
{
	struct path old_path;
	int err;

	if (!old_name || !*old_name)
		return -EINVAL;

	err = kern_path(old_name, LOOKUP_FOLLOW, &old_path);
	if (err)
		return err;

	err = do_move_mount(&old_path, path, 0);
	path_put(&old_path);
	return err;
}

/*
 * add a mount into a namespace's mount tree
 */
static int do_add_mount(struct mount *newmnt, struct mountpoint *mp,
			const struct path *path, int mnt_flags)
{
	struct mount *parent = real_mount(path->mnt);

	mnt_flags &= ~MNT_INTERNAL_FLAGS;

	if (unlikely(!check_mnt(parent))) {
		/* that's acceptable only for automounts done in private ns */
		if (!(mnt_flags & MNT_SHRINKABLE))
			return -EINVAL;
		/* ... and for those we'd better have mountpoint still alive */
		if (!parent->mnt_ns)
			return -EINVAL;
	}

	/* Refuse the same filesystem on the same mount point */
	if (path->mnt->mnt_sb == newmnt->mnt.mnt_sb && path_mounted(path))
		return -EBUSY;

	if (d_is_symlink(newmnt->mnt.mnt_root))
		return -EINVAL;

	newmnt->mnt.mnt_flags = mnt_flags;
	return graft_tree(newmnt, parent, mp);
}

static bool mount_too_revealing(const struct super_block *sb, int *new_mnt_flags);

/*
 * Create a new mount using a superblock configuration and request it
 * be added to the namespace tree.
 */
static int do_new_mount_fc(struct fs_context *fc, struct path *mountpoint,
			   unsigned int mnt_flags)
{
	struct vfsmount *mnt;
	struct pinned_mountpoint mp = {};
	struct super_block *sb = fc->root->d_sb;
	int error;

	error = security_sb_kern_mount(sb);
	if (!error && mount_too_revealing(sb, &mnt_flags))
		error = -EPERM;

	if (unlikely(error)) {
		fc_drop_locked(fc);
		return error;
	}

	up_write(&sb->s_umount);

	mnt = vfs_create_mount(fc);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);

	mnt_warn_timestamp_expiry(mountpoint, mnt);

	error = lock_mount(mountpoint, &mp);
	if (!error) {
		error = do_add_mount(real_mount(mnt), mp.mp,
				     mountpoint, mnt_flags);
		unlock_mount(&mp);
	}
	if (error < 0)
		mntput(mnt);
	return error;
}

/*
 * create a new mount for userspace and request it to be added into the
 * namespace's tree
 */
static int do_new_mount(struct path *path, const char *fstype, int sb_flags,
			int mnt_flags, const char *name, void *data)
{
	struct file_system_type *type;
	struct fs_context *fc;
	const char *subtype = NULL;
	int err = 0;

	if (!fstype)
		return -EINVAL;

	type = get_fs_type(fstype);
	if (!type)
		return -ENODEV;

	if (type->fs_flags & FS_HAS_SUBTYPE) {
		subtype = strchr(fstype, '.');
		if (subtype) {
			subtype++;
			if (!*subtype) {
				put_filesystem(type);
				return -EINVAL;
			}
		}
	}

	fc = fs_context_for_mount(type, sb_flags);
	put_filesystem(type);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

	/*
	 * Indicate to the filesystem that the mount request is coming
	 * from the legacy mount system call.
	 */
	fc->oldapi = true;

	if (subtype)
		err = vfs_parse_fs_string(fc, "subtype",
					  subtype, strlen(subtype));
	if (!err && name)
		err = vfs_parse_fs_string(fc, "source", name, strlen(name));
	if (!err)
		err = parse_monolithic_mount_data(fc, data);
	if (!err && !mount_capable(fc))
		err = -EPERM;
	if (!err)
		err = vfs_get_tree(fc);
	if (!err)
		err = do_new_mount_fc(fc, path, mnt_flags);

	put_fs_context(fc);
	return err;
}

int finish_automount(struct vfsmount *m, const struct path *path)
{
	struct dentry *dentry = path->dentry;
	struct pinned_mountpoint mp = {};
	struct mount *mnt;
	int err;

	if (!m)
		return 0;
	if (IS_ERR(m))
		return PTR_ERR(m);

	mnt = real_mount(m);

	if (m->mnt_sb == path->mnt->mnt_sb &&
	    m->mnt_root == dentry) {
		err = -ELOOP;
		goto discard;
	}

	/*
	 * we don't want to use lock_mount() - in this case finding something
	 * that overmounts our mountpoint to be means "quitely drop what we've
	 * got", not "try to mount it on top".
	 */
	inode_lock(dentry->d_inode);
	namespace_lock();
	if (unlikely(cant_mount(dentry))) {
		err = -ENOENT;
		goto discard_locked;
	}
	if (path_overmounted(path)) {
		err = 0;
		goto discard_locked;
	}
	err = get_mountpoint(dentry, &mp);
	if (err)
		goto discard_locked;

	err = do_add_mount(mnt, mp.mp, path,
			   path->mnt->mnt_flags | MNT_SHRINKABLE);
	unlock_mount(&mp);
	if (unlikely(err))
		goto discard;
	return 0;

discard_locked:
	namespace_unlock();
	inode_unlock(dentry->d_inode);
discard:
	mntput(m);
	return err;
}

/**
 * mnt_set_expiry - Put a mount on an expiration list
 * @mnt: The mount to list.
 * @expiry_list: The list to add the mount to.
 */
void mnt_set_expiry(struct vfsmount *mnt, struct list_head *expiry_list)
{
	read_seqlock_excl(&mount_lock);
	list_add_tail(&real_mount(mnt)->mnt_expire, expiry_list);
	read_sequnlock_excl(&mount_lock);
}
EXPORT_SYMBOL(mnt_set_expiry);

/*
 * process a list of expirable mountpoints with the intent of discarding any
 * mountpoints that aren't in use and haven't been touched since last we came
 * here
 */
void mark_mounts_for_expiry(struct list_head *mounts)
{
	struct mount *mnt, *next;
	LIST_HEAD(graveyard);

	if (list_empty(mounts))
		return;

	namespace_lock();
	lock_mount_hash();

	/* extract from the expiration list every vfsmount that matches the
	 * following criteria:
	 * - already mounted
	 * - only referenced by its parent vfsmount
	 * - still marked for expiry (marked on the last call here; marks are
	 *   cleared by mntput())
	 */
	list_for_each_entry_safe(mnt, next, mounts, mnt_expire) {
		if (!is_mounted(&mnt->mnt))
			continue;
		if (!xchg(&mnt->mnt_expiry_mark, 1) ||
			propagate_mount_busy(mnt, 1))
			continue;
		list_move(&mnt->mnt_expire, &graveyard);
	}
	while (!list_empty(&graveyard)) {
		mnt = list_first_entry(&graveyard, struct mount, mnt_expire);
		touch_mnt_namespace(mnt->mnt_ns);
		umount_tree(mnt, UMOUNT_PROPAGATE|UMOUNT_SYNC);
	}
	unlock_mount_hash();
	namespace_unlock();
}

EXPORT_SYMBOL_GPL(mark_mounts_for_expiry);

/*
 * Ripoff of 'select_parent()'
 *
 * search the list of submounts for a given mountpoint, and move any
 * shrinkable submounts to the 'graveyard' list.
 */
static int select_submounts(struct mount *parent, struct list_head *graveyard)
{
	struct mount *this_parent = parent;
	struct list_head *next;
	int found = 0;

repeat:
	next = this_parent->mnt_mounts.next;
resume:
	while (next != &this_parent->mnt_mounts) {
		struct list_head *tmp = next;
		struct mount *mnt = list_entry(tmp, struct mount, mnt_child);

		next = tmp->next;
		if (!(mnt->mnt.mnt_flags & MNT_SHRINKABLE))
			continue;
		/*
		 * Descend a level if the d_mounts list is non-empty.
		 */
		if (!list_empty(&mnt->mnt_mounts)) {
			this_parent = mnt;
			goto repeat;
		}

		if (!propagate_mount_busy(mnt, 1)) {
			list_move_tail(&mnt->mnt_expire, graveyard);
			found++;
		}
	}
	/*
	 * All done at this level ... ascend and resume the search
	 */
	if (this_parent != parent) {
		next = this_parent->mnt_child.next;
		this_parent = this_parent->mnt_parent;
		goto resume;
	}
	return found;
}

/*
 * process a list of expirable mountpoints with the intent of discarding any
 * submounts of a specific parent mountpoint
 *
 * mount_lock must be held for write
 */
static void shrink_submounts(struct mount *mnt)
{
	LIST_HEAD(graveyard);
	struct mount *m;

	/* extract submounts of 'mountpoint' from the expiration list */
	while (select_submounts(mnt, &graveyard)) {
		while (!list_empty(&graveyard)) {
			m = list_first_entry(&graveyard, struct mount,
						mnt_expire);
			touch_mnt_namespace(m->mnt_ns);
			umount_tree(m, UMOUNT_PROPAGATE|UMOUNT_SYNC);
		}
	}
}

static void *copy_mount_options(const void __user * data)
{
	char *copy;
	unsigned left, offset;

	if (!data)
		return NULL;

	copy = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!copy)
		return ERR_PTR(-ENOMEM);

	left = copy_from_user(copy, data, PAGE_SIZE);

	/*
	 * Not all architectures have an exact copy_from_user(). Resort to
	 * byte at a time.
	 */
	offset = PAGE_SIZE - left;
	while (left) {
		char c;
		if (get_user(c, (const char __user *)data + offset))
			break;
		copy[offset] = c;
		left--;
		offset++;
	}

	if (left == PAGE_SIZE) {
		kfree(copy);
		return ERR_PTR(-EFAULT);
	}

	return copy;
}

static char *copy_mount_string(const void __user *data)
{
	return data ? strndup_user(data, PATH_MAX) : NULL;
}

/*
 * Flags is a 32-bit value that allows up to 31 non-fs dependent flags to
 * be given to the mount() call (ie: read-only, no-dev, no-suid etc).
 *
 * data is a (void *) that can point to any structure up to
 * PAGE_SIZE-1 bytes, which can contain arbitrary fs-dependent
 * information (or be NULL).
 *
 * Pre-0.97 versions of mount() didn't have a flags word.
 * When the flags word was introduced its top half was required
 * to have the magic value 0xC0ED, and this remained so until 2.4.0-test9.
 * Therefore, if this magic number is present, it carries no information
 * and must be discarded.
 */
int path_mount(const char *dev_name, struct path *path,
		const char *type_page, unsigned long flags, void *data_page)
{
	unsigned int mnt_flags = 0, sb_flags;
	int ret;

	/* Discard magic */
	if ((flags & MS_MGC_MSK) == MS_MGC_VAL)
		flags &= ~MS_MGC_MSK;

	/* Basic sanity checks */
	if (data_page)
		((char *)data_page)[PAGE_SIZE - 1] = 0;

	if (flags & MS_NOUSER)
		return -EINVAL;

	ret = security_sb_mount(dev_name, path, type_page, flags, data_page);
	if (ret)
		return ret;
	if (!may_mount())
		return -EPERM;
	if (flags & SB_MANDLOCK)
		warn_mandlock();

	/* Default to relatime unless overriden */
	if (!(flags & MS_NOATIME))
		mnt_flags |= MNT_RELATIME;

	/* Separate the per-mountpoint flags */
	if (flags & MS_NOSUID)
		mnt_flags |= MNT_NOSUID;
	if (flags & MS_NODEV)
		mnt_flags |= MNT_NODEV;
	if (flags & MS_NOEXEC)
		mnt_flags |= MNT_NOEXEC;
	if (flags & MS_NOATIME)
		mnt_flags |= MNT_NOATIME;
	if (flags & MS_NODIRATIME)
		mnt_flags |= MNT_NODIRATIME;
	if (flags & MS_STRICTATIME)
		mnt_flags &= ~(MNT_RELATIME | MNT_NOATIME);
	if (flags & MS_RDONLY)
		mnt_flags |= MNT_READONLY;
	if (flags & MS_NOSYMFOLLOW)
		mnt_flags |= MNT_NOSYMFOLLOW;

	/* The default atime for remount is preservation */
	if ((flags & MS_REMOUNT) &&
	    ((flags & (MS_NOATIME | MS_NODIRATIME | MS_RELATIME |
		       MS_STRICTATIME)) == 0)) {
		mnt_flags &= ~MNT_ATIME_MASK;
		mnt_flags |= path->mnt->mnt_flags & MNT_ATIME_MASK;
	}

	sb_flags = flags & (SB_RDONLY |
			    SB_SYNCHRONOUS |
			    SB_MANDLOCK |
			    SB_DIRSYNC |
			    SB_SILENT |
			    SB_POSIXACL |
			    SB_LAZYTIME |
			    SB_I_VERSION);

	if ((flags & (MS_REMOUNT | MS_BIND)) == (MS_REMOUNT | MS_BIND))
		return do_reconfigure_mnt(path, mnt_flags);
	if (flags & MS_REMOUNT)
		return do_remount(path, flags, sb_flags, mnt_flags, data_page);
	if (flags & MS_BIND)
		return do_loopback(path, dev_name, flags & MS_REC);
	if (flags & (MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE))
		return do_change_type(path, flags);
	if (flags & MS_MOVE)
		return do_move_mount_old(path, dev_name);

	return do_new_mount(path, type_page, sb_flags, mnt_flags, dev_name,
			    data_page);
}

int do_mount(const char *dev_name, const char __user *dir_name,
		const char *type_page, unsigned long flags, void *data_page)
{
	struct path path;
	int ret;

	ret = user_path_at(AT_FDCWD, dir_name, LOOKUP_FOLLOW, &path);
	if (ret)
		return ret;
	ret = path_mount(dev_name, &path, type_page, flags, data_page);
	path_put(&path);
	return ret;
}

static struct ucounts *inc_mnt_namespaces(struct user_namespace *ns)
{
	return inc_ucount(ns, current_euid(), UCOUNT_MNT_NAMESPACES);
}

static void dec_mnt_namespaces(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_MNT_NAMESPACES);
}

static void free_mnt_ns(struct mnt_namespace *ns)
{
	if (!is_anon_ns(ns))
		ns_free_inum(&ns->ns);
	dec_mnt_namespaces(ns->ucounts);
	mnt_ns_tree_remove(ns);
}

/*
 * Assign a sequence number so we can detect when we attempt to bind
 * mount a reference to an older mount namespace into the current
 * mount namespace, preventing reference counting loops.  A 64bit
 * number incrementing at 10Ghz will take 12,427 years to wrap which
 * is effectively never, so we can ignore the possibility.
 */
static atomic64_t mnt_ns_seq = ATOMIC64_INIT(1);

static struct mnt_namespace *alloc_mnt_ns(struct user_namespace *user_ns, bool anon)
{
	struct mnt_namespace *new_ns;
	struct ucounts *ucounts;
	int ret;

	ucounts = inc_mnt_namespaces(user_ns);
	if (!ucounts)
		return ERR_PTR(-ENOSPC);

	new_ns = kzalloc(sizeof(struct mnt_namespace), GFP_KERNEL_ACCOUNT);
	if (!new_ns) {
		dec_mnt_namespaces(ucounts);
		return ERR_PTR(-ENOMEM);
	}
	if (!anon) {
		ret = ns_alloc_inum(&new_ns->ns);
		if (ret) {
			kfree(new_ns);
			dec_mnt_namespaces(ucounts);
			return ERR_PTR(ret);
		}
	}
	new_ns->ns.ops = &mntns_operations;
	if (!anon)
		new_ns->seq = atomic64_inc_return(&mnt_ns_seq);
	refcount_set(&new_ns->ns.count, 1);
	refcount_set(&new_ns->passive, 1);
	new_ns->mounts = RB_ROOT;
	INIT_LIST_HEAD(&new_ns->mnt_ns_list);
	RB_CLEAR_NODE(&new_ns->mnt_ns_tree_node);
	init_waitqueue_head(&new_ns->poll);
	new_ns->user_ns = get_user_ns(user_ns);
	new_ns->ucounts = ucounts;
	return new_ns;
}

__latent_entropy
struct mnt_namespace *copy_mnt_ns(unsigned long flags, struct mnt_namespace *ns,
		struct user_namespace *user_ns, struct fs_struct *new_fs)
{
	struct mnt_namespace *new_ns;
	struct vfsmount *rootmnt = NULL, *pwdmnt = NULL;
	struct mount *p, *q;
	struct mount *old;
	struct mount *new;
	int copy_flags;

	BUG_ON(!ns);

	if (likely(!(flags & CLONE_NEWNS))) {
		get_mnt_ns(ns);
		return ns;
	}

	old = ns->root;

	new_ns = alloc_mnt_ns(user_ns, false);
	if (IS_ERR(new_ns))
		return new_ns;

	namespace_lock();
	/* First pass: copy the tree topology */
	copy_flags = CL_COPY_UNBINDABLE | CL_EXPIRE;
	if (user_ns != ns->user_ns)
		copy_flags |= CL_SLAVE;
	new = copy_tree(old, old->mnt.mnt_root, copy_flags);
	if (IS_ERR(new)) {
		namespace_unlock();
		ns_free_inum(&new_ns->ns);
		dec_mnt_namespaces(new_ns->ucounts);
		mnt_ns_release(new_ns);
		return ERR_CAST(new);
	}
	if (user_ns != ns->user_ns) {
		lock_mount_hash();
		lock_mnt_tree(new);
		unlock_mount_hash();
	}
	new_ns->root = new;

	/*
	 * Second pass: switch the tsk->fs->* elements and mark new vfsmounts
	 * as belonging to new namespace.  We have already acquired a private
	 * fs_struct, so tsk->fs->lock is not needed.
	 */
	p = old;
	q = new;
	while (p) {
		mnt_add_to_ns(new_ns, q);
		new_ns->nr_mounts++;
		if (new_fs) {
			if (&p->mnt == new_fs->root.mnt) {
				new_fs->root.mnt = mntget(&q->mnt);
				rootmnt = &p->mnt;
			}
			if (&p->mnt == new_fs->pwd.mnt) {
				new_fs->pwd.mnt = mntget(&q->mnt);
				pwdmnt = &p->mnt;
			}
		}
		p = next_mnt(p, old);
		q = next_mnt(q, new);
		if (!q)
			break;
		// an mntns binding we'd skipped?
		while (p->mnt.mnt_root != q->mnt.mnt_root)
			p = next_mnt(skip_mnt_tree(p), old);
	}
	namespace_unlock();

	if (rootmnt)
		mntput(rootmnt);
	if (pwdmnt)
		mntput(pwdmnt);

	mnt_ns_tree_add(new_ns);
	return new_ns;
}

struct dentry *mount_subtree(struct vfsmount *m, const char *name)
{
	struct mount *mnt = real_mount(m);
	struct mnt_namespace *ns;
	struct super_block *s;
	struct path path;
	int err;

	ns = alloc_mnt_ns(&init_user_ns, true);
	if (IS_ERR(ns)) {
		mntput(m);
		return ERR_CAST(ns);
	}
	ns->root = mnt;
	ns->nr_mounts++;
	mnt_add_to_ns(ns, mnt);

	err = vfs_path_lookup(m->mnt_root, m,
			name, LOOKUP_FOLLOW|LOOKUP_AUTOMOUNT, &path);

	put_mnt_ns(ns);

	if (err)
		return ERR_PTR(err);

	/* trade a vfsmount reference for active sb one */
	s = path.mnt->mnt_sb;
	atomic_inc(&s->s_active);
	mntput(path.mnt);
	/* lock the sucker */
	down_write(&s->s_umount);
	/* ... and return the root of (sub)tree on it */
	return path.dentry;
}
EXPORT_SYMBOL(mount_subtree);

SYSCALL_DEFINE5(mount, char __user *, dev_name, char __user *, dir_name,
		char __user *, type, unsigned long, flags, void __user *, data)
{
	int ret;
	char *kernel_type;
	char *kernel_dev;
	void *options;

	kernel_type = copy_mount_string(type);
	ret = PTR_ERR(kernel_type);
	if (IS_ERR(kernel_type))
		goto out_type;

	kernel_dev = copy_mount_string(dev_name);
	ret = PTR_ERR(kernel_dev);
	if (IS_ERR(kernel_dev))
		goto out_dev;

	options = copy_mount_options(data);
	ret = PTR_ERR(options);
	if (IS_ERR(options))
		goto out_data;

	ret = do_mount(kernel_dev, dir_name, kernel_type, flags, options);

	kfree(options);
out_data:
	kfree(kernel_dev);
out_dev:
	kfree(kernel_type);
out_type:
	return ret;
}

#define FSMOUNT_VALID_FLAGS                                                    \
	(MOUNT_ATTR_RDONLY | MOUNT_ATTR_NOSUID | MOUNT_ATTR_NODEV |            \
	 MOUNT_ATTR_NOEXEC | MOUNT_ATTR__ATIME | MOUNT_ATTR_NODIRATIME |       \
	 MOUNT_ATTR_NOSYMFOLLOW)

#define MOUNT_SETATTR_VALID_FLAGS (FSMOUNT_VALID_FLAGS | MOUNT_ATTR_IDMAP)

#define MOUNT_SETATTR_PROPAGATION_FLAGS \
	(MS_UNBINDABLE | MS_PRIVATE | MS_SLAVE | MS_SHARED)

static unsigned int attr_flags_to_mnt_flags(u64 attr_flags)
{
	unsigned int mnt_flags = 0;

	if (attr_flags & MOUNT_ATTR_RDONLY)
		mnt_flags |= MNT_READONLY;
	if (attr_flags & MOUNT_ATTR_NOSUID)
		mnt_flags |= MNT_NOSUID;
	if (attr_flags & MOUNT_ATTR_NODEV)
		mnt_flags |= MNT_NODEV;
	if (attr_flags & MOUNT_ATTR_NOEXEC)
		mnt_flags |= MNT_NOEXEC;
	if (attr_flags & MOUNT_ATTR_NODIRATIME)
		mnt_flags |= MNT_NODIRATIME;
	if (attr_flags & MOUNT_ATTR_NOSYMFOLLOW)
		mnt_flags |= MNT_NOSYMFOLLOW;

	return mnt_flags;
}

/*
 * Create a kernel mount representation for a new, prepared superblock
 * (specified by fs_fd) and attach to an open_tree-like file descriptor.
 */
SYSCALL_DEFINE3(fsmount, int, fs_fd, unsigned int, flags,
		unsigned int, attr_flags)
{
	struct mnt_namespace *ns;
	struct fs_context *fc;
	struct file *file;
	struct path newmount;
	struct mount *mnt;
	unsigned int mnt_flags = 0;
	long ret;

	if (!may_mount())
		return -EPERM;

	if ((flags & ~(FSMOUNT_CLOEXEC)) != 0)
		return -EINVAL;

	if (attr_flags & ~FSMOUNT_VALID_FLAGS)
		return -EINVAL;

	mnt_flags = attr_flags_to_mnt_flags(attr_flags);

	switch (attr_flags & MOUNT_ATTR__ATIME) {
	case MOUNT_ATTR_STRICTATIME:
		break;
	case MOUNT_ATTR_NOATIME:
		mnt_flags |= MNT_NOATIME;
		break;
	case MOUNT_ATTR_RELATIME:
		mnt_flags |= MNT_RELATIME;
		break;
	default:
		return -EINVAL;
	}

	CLASS(fd, f)(fs_fd);
	if (fd_empty(f))
		return -EBADF;

	if (fd_file(f)->f_op != &fscontext_fops)
		return -EINVAL;

	fc = fd_file(f)->private_data;

	ret = mutex_lock_interruptible(&fc->uapi_mutex);
	if (ret < 0)
		return ret;

	/* There must be a valid superblock or we can't mount it */
	ret = -EINVAL;
	if (!fc->root)
		goto err_unlock;

	ret = -EPERM;
	if (mount_too_revealing(fc->root->d_sb, &mnt_flags)) {
		pr_warn("VFS: Mount too revealing\n");
		goto err_unlock;
	}

	ret = -EBUSY;
	if (fc->phase != FS_CONTEXT_AWAITING_MOUNT)
		goto err_unlock;

	if (fc->sb_flags & SB_MANDLOCK)
		warn_mandlock();

	newmount.mnt = vfs_create_mount(fc);
	if (IS_ERR(newmount.mnt)) {
		ret = PTR_ERR(newmount.mnt);
		goto err_unlock;
	}
	newmount.dentry = dget(fc->root);
	newmount.mnt->mnt_flags = mnt_flags;

	/* We've done the mount bit - now move the file context into more or
	 * less the same state as if we'd done an fspick().  We don't want to
	 * do any memory allocation or anything like that at this point as we
	 * don't want to have to handle any errors incurred.
	 */
	vfs_clean_context(fc);

	ns = alloc_mnt_ns(current->nsproxy->mnt_ns->user_ns, true);
	if (IS_ERR(ns)) {
		ret = PTR_ERR(ns);
		goto err_path;
	}
	mnt = real_mount(newmount.mnt);
	ns->root = mnt;
	ns->nr_mounts = 1;
	mnt_add_to_ns(ns, mnt);
	mntget(newmount.mnt);

	/* Attach to an apparent O_PATH fd with a note that we need to unmount
	 * it, not just simply put it.
	 */
	file = dentry_open(&newmount, O_PATH, fc->cred);
	if (IS_ERR(file)) {
		dissolve_on_fput(newmount.mnt);
		ret = PTR_ERR(file);
		goto err_path;
	}
	file->f_mode |= FMODE_NEED_UNMOUNT;

	ret = get_unused_fd_flags((flags & FSMOUNT_CLOEXEC) ? O_CLOEXEC : 0);
	if (ret >= 0)
		fd_install(ret, file);
	else
		fput(file);

err_path:
	path_put(&newmount);
err_unlock:
	mutex_unlock(&fc->uapi_mutex);
	return ret;
}

static inline int vfs_move_mount(struct path *from_path, struct path *to_path,
				 enum mnt_tree_flags_t mflags)
{
	int ret;

	ret = security_move_mount(from_path, to_path);
	if (ret)
		return ret;

	if (mflags & MNT_TREE_PROPAGATION)
		return do_set_group(from_path, to_path);

	return do_move_mount(from_path, to_path, mflags);
}

/*
 * Move a mount from one place to another.  In combination with
 * fsopen()/fsmount() this is used to install a new mount and in combination
 * with open_tree(OPEN_TREE_CLONE [| AT_RECURSIVE]) it can be used to copy
 * a mount subtree.
 *
 * Note the flags value is a combination of MOVE_MOUNT_* flags.
 */
SYSCALL_DEFINE5(move_mount,
		int, from_dfd, const char __user *, from_pathname,
		int, to_dfd, const char __user *, to_pathname,
		unsigned int, flags)
{
	struct path to_path __free(path_put) = {};
	struct path from_path __free(path_put) = {};
	struct filename *to_name __free(putname) = NULL;
	struct filename *from_name __free(putname) = NULL;
	unsigned int lflags, uflags;
	enum mnt_tree_flags_t mflags = 0;
	int ret = 0;

	if (!may_mount())
		return -EPERM;

	if (flags & ~MOVE_MOUNT__MASK)
		return -EINVAL;

	if ((flags & (MOVE_MOUNT_BENEATH | MOVE_MOUNT_SET_GROUP)) ==
	    (MOVE_MOUNT_BENEATH | MOVE_MOUNT_SET_GROUP))
		return -EINVAL;

	if (flags & MOVE_MOUNT_SET_GROUP)	mflags |= MNT_TREE_PROPAGATION;
	if (flags & MOVE_MOUNT_BENEATH)		mflags |= MNT_TREE_BENEATH;

	lflags = 0;
	if (flags & MOVE_MOUNT_F_SYMLINKS)	lflags |= LOOKUP_FOLLOW;
	if (flags & MOVE_MOUNT_F_AUTOMOUNTS)	lflags |= LOOKUP_AUTOMOUNT;
	uflags = 0;
	if (flags & MOVE_MOUNT_F_EMPTY_PATH)	uflags = AT_EMPTY_PATH;
	from_name = getname_maybe_null(from_pathname, uflags);
	if (IS_ERR(from_name))
		return PTR_ERR(from_name);

	lflags = 0;
	if (flags & MOVE_MOUNT_T_SYMLINKS)	lflags |= LOOKUP_FOLLOW;
	if (flags & MOVE_MOUNT_T_AUTOMOUNTS)	lflags |= LOOKUP_AUTOMOUNT;
	uflags = 0;
	if (flags & MOVE_MOUNT_T_EMPTY_PATH)	uflags = AT_EMPTY_PATH;
	to_name = getname_maybe_null(to_pathname, uflags);
	if (IS_ERR(to_name))
		return PTR_ERR(to_name);

	if (!to_name && to_dfd >= 0) {
		CLASS(fd_raw, f_to)(to_dfd);
		if (fd_empty(f_to))
			return -EBADF;

		to_path = fd_file(f_to)->f_path;
		path_get(&to_path);
	} else {
		ret = filename_lookup(to_dfd, to_name, lflags, &to_path, NULL);
		if (ret)
			return ret;
	}

	if (!from_name && from_dfd >= 0) {
		CLASS(fd_raw, f_from)(from_dfd);
		if (fd_empty(f_from))
			return -EBADF;

		return vfs_move_mount(&fd_file(f_from)->f_path, &to_path, mflags);
	}

	ret = filename_lookup(from_dfd, from_name, lflags, &from_path, NULL);
	if (ret)
		return ret;

	return vfs_move_mount(&from_path, &to_path, mflags);
}

/*
 * Return true if path is reachable from root
 *
 * namespace_sem or mount_lock is held
 */
bool is_path_reachable(struct mount *mnt, struct dentry *dentry,
			 const struct path *root)
{
	while (&mnt->mnt != root->mnt && mnt_has_parent(mnt)) {
		dentry = mnt->mnt_mountpoint;
		mnt = mnt->mnt_parent;
	}
	return &mnt->mnt == root->mnt && is_subdir(dentry, root->dentry);
}

bool path_is_under(const struct path *path1, const struct path *path2)
{
	bool res;
	read_seqlock_excl(&mount_lock);
	res = is_path_reachable(real_mount(path1->mnt), path1->dentry, path2);
	read_sequnlock_excl(&mount_lock);
	return res;
}
EXPORT_SYMBOL(path_is_under);

/*
 * pivot_root Semantics:
 * Moves the root file system of the current process to the directory put_old,
 * makes new_root as the new root file system of the current process, and sets
 * root/cwd of all processes which had them on the current root to new_root.
 *
 * Restrictions:
 * The new_root and put_old must be directories, and  must not be on the
 * same file  system as the current process root. The put_old  must  be
 * underneath new_root,  i.e. adding a non-zero number of /.. to the string
 * pointed to by put_old must yield the same directory as new_root. No other
 * file system may be mounted on put_old. After all, new_root is a mountpoint.
 *
 * Also, the current root cannot be on the 'rootfs' (initial ramfs) filesystem.
 * See Documentation/filesystems/ramfs-rootfs-initramfs.rst for alternatives
 * in this situation.
 *
 * Notes:
 *  - we don't move root/cwd if they are not at the root (reason: if something
 *    cared enough to change them, it's probably wrong to force them elsewhere)
 *  - it's okay to pick a root that isn't the root of a file system, e.g.
 *    /nfs/my_root where /nfs is the mount point. It must be a mountpoint,
 *    though, so you may need to say mount --bind /nfs/my_root /nfs/my_root
 *    first.
 */
SYSCALL_DEFINE2(pivot_root, const char __user *, new_root,
		const char __user *, put_old)
{
	struct path new, old, root;
	struct mount *new_mnt, *root_mnt, *old_mnt, *root_parent, *ex_parent;
	struct pinned_mountpoint old_mp = {};
	int error;

	if (!may_mount())
		return -EPERM;

	error = user_path_at(AT_FDCWD, new_root,
			     LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &new);
	if (error)
		goto out0;

	error = user_path_at(AT_FDCWD, put_old,
			     LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &old);
	if (error)
		goto out1;

	error = security_sb_pivotroot(&old, &new);
	if (error)
		goto out2;

	get_fs_root(current->fs, &root);
	error = lock_mount(&old, &old_mp);
	if (error)
		goto out3;

	error = -EINVAL;
	new_mnt = real_mount(new.mnt);
	root_mnt = real_mount(root.mnt);
	old_mnt = real_mount(old.mnt);
	ex_parent = new_mnt->mnt_parent;
	root_parent = root_mnt->mnt_parent;
	if (IS_MNT_SHARED(old_mnt) ||
		IS_MNT_SHARED(ex_parent) ||
		IS_MNT_SHARED(root_parent))
		goto out4;
	if (!check_mnt(root_mnt) || !check_mnt(new_mnt))
		goto out4;
	if (new_mnt->mnt.mnt_flags & MNT_LOCKED)
		goto out4;
	error = -ENOENT;
	if (d_unlinked(new.dentry))
		goto out4;
	error = -EBUSY;
	if (new_mnt == root_mnt || old_mnt == root_mnt)
		goto out4; /* loop, on the same file system  */
	error = -EINVAL;
	if (!path_mounted(&root))
		goto out4; /* not a mountpoint */
	if (!mnt_has_parent(root_mnt))
		goto out4; /* absolute root */
	if (!path_mounted(&new))
		goto out4; /* not a mountpoint */
	if (!mnt_has_parent(new_mnt))
		goto out4; /* absolute root */
	/* make sure we can reach put_old from new_root */
	if (!is_path_reachable(old_mnt, old.dentry, &new))
		goto out4;
	/* make certain new is below the root */
	if (!is_path_reachable(new_mnt, new.dentry, &root))
		goto out4;
	lock_mount_hash();
	umount_mnt(new_mnt);
	if (root_mnt->mnt.mnt_flags & MNT_LOCKED) {
		new_mnt->mnt.mnt_flags |= MNT_LOCKED;
		root_mnt->mnt.mnt_flags &= ~MNT_LOCKED;
	}
	/* mount new_root on / */
	attach_mnt(new_mnt, root_parent, root_mnt->mnt_mp);
	umount_mnt(root_mnt);
	/* mount old root on put_old */
	attach_mnt(root_mnt, old_mnt, old_mp.mp);
	touch_mnt_namespace(current->nsproxy->mnt_ns);
	/* A moved mount should not expire automatically */
	list_del_init(&new_mnt->mnt_expire);
	unlock_mount_hash();
	mnt_notify_add(root_mnt);
	mnt_notify_add(new_mnt);
	chroot_fs_refs(&root, &new);
	error = 0;
out4:
	unlock_mount(&old_mp);
out3:
	path_put(&root);
out2:
	path_put(&old);
out1:
	path_put(&new);
out0:
	return error;
}

static unsigned int recalc_flags(struct mount_kattr *kattr, struct mount *mnt)
{
	unsigned int flags = mnt->mnt.mnt_flags;

	/*  flags to clear */
	flags &= ~kattr->attr_clr;
	/* flags to raise */
	flags |= kattr->attr_set;

	return flags;
}

static int can_idmap_mount(const struct mount_kattr *kattr, struct mount *mnt)
{
	struct vfsmount *m = &mnt->mnt;
	struct user_namespace *fs_userns = m->mnt_sb->s_user_ns;

	if (!kattr->mnt_idmap)
		return 0;

	/*
	 * Creating an idmapped mount with the filesystem wide idmapping
	 * doesn't make sense so block that. We don't allow mushy semantics.
	 */
	if (kattr->mnt_userns == m->mnt_sb->s_user_ns)
		return -EINVAL;

	/*
	 * We only allow an mount to change it's idmapping if it has
	 * never been accessible to userspace.
	 */
	if (!(kattr->kflags & MOUNT_KATTR_IDMAP_REPLACE) && is_idmapped_mnt(m))
		return -EPERM;

	/* The underlying filesystem doesn't support idmapped mounts yet. */
	if (!(m->mnt_sb->s_type->fs_flags & FS_ALLOW_IDMAP))
		return -EINVAL;

	/* The filesystem has turned off idmapped mounts. */
	if (m->mnt_sb->s_iflags & SB_I_NOIDMAP)
		return -EINVAL;

	/* We're not controlling the superblock. */
	if (!ns_capable(fs_userns, CAP_SYS_ADMIN))
		return -EPERM;

	/* Mount has already been visible in the filesystem hierarchy. */
	if (!is_anon_ns(mnt->mnt_ns))
		return -EINVAL;

	return 0;
}

/**
 * mnt_allow_writers() - check whether the attribute change allows writers
 * @kattr: the new mount attributes
 * @mnt: the mount to which @kattr will be applied
 *
 * Check whether thew new mount attributes in @kattr allow concurrent writers.
 *
 * Return: true if writers need to be held, false if not
 */
static inline bool mnt_allow_writers(const struct mount_kattr *kattr,
				     const struct mount *mnt)
{
	return (!(kattr->attr_set & MNT_READONLY) ||
		(mnt->mnt.mnt_flags & MNT_READONLY)) &&
	       !kattr->mnt_idmap;
}

static int mount_setattr_prepare(struct mount_kattr *kattr, struct mount *mnt)
{
	struct mount *m;
	int err;

	for (m = mnt; m; m = next_mnt(m, mnt)) {
		if (!can_change_locked_flags(m, recalc_flags(kattr, m))) {
			err = -EPERM;
			break;
		}

		err = can_idmap_mount(kattr, m);
		if (err)
			break;

		if (!mnt_allow_writers(kattr, m)) {
			err = mnt_hold_writers(m);
			if (err)
				break;
		}

		if (!(kattr->kflags & MOUNT_KATTR_RECURSE))
			return 0;
	}

	if (err) {
		struct mount *p;

		/*
		 * If we had to call mnt_hold_writers() MNT_WRITE_HOLD will
		 * be set in @mnt_flags. The loop unsets MNT_WRITE_HOLD for all
		 * mounts and needs to take care to include the first mount.
		 */
		for (p = mnt; p; p = next_mnt(p, mnt)) {
			/* If we had to hold writers unblock them. */
			if (p->mnt.mnt_flags & MNT_WRITE_HOLD)
				mnt_unhold_writers(p);

			/*
			 * We're done once the first mount we changed got
			 * MNT_WRITE_HOLD unset.
			 */
			if (p == m)
				break;
		}
	}
	return err;
}

static void do_idmap_mount(const struct mount_kattr *kattr, struct mount *mnt)
{
	struct mnt_idmap *old_idmap;

	if (!kattr->mnt_idmap)
		return;

	old_idmap = mnt_idmap(&mnt->mnt);

	/* Pairs with smp_load_acquire() in mnt_idmap(). */
	smp_store_release(&mnt->mnt.mnt_idmap, mnt_idmap_get(kattr->mnt_idmap));
	mnt_idmap_put(old_idmap);
}

static void mount_setattr_commit(struct mount_kattr *kattr, struct mount *mnt)
{
	struct mount *m;

	for (m = mnt; m; m = next_mnt(m, mnt)) {
		unsigned int flags;

		do_idmap_mount(kattr, m);
		flags = recalc_flags(kattr, m);
		WRITE_ONCE(m->mnt.mnt_flags, flags);

		/* If we had to hold writers unblock them. */
		if (m->mnt.mnt_flags & MNT_WRITE_HOLD)
			mnt_unhold_writers(m);

		if (kattr->propagation)
			change_mnt_propagation(m, kattr->propagation);
		if (!(kattr->kflags & MOUNT_KATTR_RECURSE))
			break;
	}
	touch_mnt_namespace(mnt->mnt_ns);
}

static int do_mount_setattr(struct path *path, struct mount_kattr *kattr)
{
	struct mount *mnt = real_mount(path->mnt);
	int err = 0;

	if (!path_mounted(path))
		return -EINVAL;

	if (kattr->mnt_userns) {
		struct mnt_idmap *mnt_idmap;

		mnt_idmap = alloc_mnt_idmap(kattr->mnt_userns);
		if (IS_ERR(mnt_idmap))
			return PTR_ERR(mnt_idmap);
		kattr->mnt_idmap = mnt_idmap;
	}

	if (kattr->propagation) {
		/*
		 * Only take namespace_lock() if we're actually changing
		 * propagation.
		 */
		namespace_lock();
		if (kattr->propagation == MS_SHARED) {
			err = invent_group_ids(mnt, kattr->kflags & MOUNT_KATTR_RECURSE);
			if (err) {
				namespace_unlock();
				return err;
			}
		}
	}

	err = -EINVAL;
	lock_mount_hash();

	if (!anon_ns_root(mnt) && !check_mnt(mnt))
		goto out;

	/*
	 * First, we get the mount tree in a shape where we can change mount
	 * properties without failure. If we succeeded to do so we commit all
	 * changes and if we failed we clean up.
	 */
	err = mount_setattr_prepare(kattr, mnt);
	if (!err)
		mount_setattr_commit(kattr, mnt);

out:
	unlock_mount_hash();

	if (kattr->propagation) {
		if (err)
			cleanup_group_ids(mnt, NULL);
		namespace_unlock();
	}

	return err;
}

static int build_mount_idmapped(const struct mount_attr *attr, size_t usize,
				struct mount_kattr *kattr)
{
	struct ns_common *ns;
	struct user_namespace *mnt_userns;

	if (!((attr->attr_set | attr->attr_clr) & MOUNT_ATTR_IDMAP))
		return 0;

	if (attr->attr_clr & MOUNT_ATTR_IDMAP) {
		/*
		 * We can only remove an idmapping if it's never been
		 * exposed to userspace.
		 */
		if (!(kattr->kflags & MOUNT_KATTR_IDMAP_REPLACE))
			return -EINVAL;

		/*
		 * Removal of idmappings is equivalent to setting
		 * nop_mnt_idmap.
		 */
		if (!(attr->attr_set & MOUNT_ATTR_IDMAP)) {
			kattr->mnt_idmap = &nop_mnt_idmap;
			return 0;
		}
	}

	if (attr->userns_fd > INT_MAX)
		return -EINVAL;

	CLASS(fd, f)(attr->userns_fd);
	if (fd_empty(f))
		return -EBADF;

	if (!proc_ns_file(fd_file(f)))
		return -EINVAL;

	ns = get_proc_ns(file_inode(fd_file(f)));
	if (ns->ops->type != CLONE_NEWUSER)
		return -EINVAL;

	/*
	 * The initial idmapping cannot be used to create an idmapped
	 * mount. We use the initial idmapping as an indicator of a mount
	 * that is not idmapped. It can simply be passed into helpers that
	 * are aware of idmapped mounts as a convenient shortcut. A user
	 * can just create a dedicated identity mapping to achieve the same
	 * result.
	 */
	mnt_userns = container_of(ns, struct user_namespace, ns);
	if (mnt_userns == &init_user_ns)
		return -EPERM;

	/* We're not controlling the target namespace. */
	if (!ns_capable(mnt_userns, CAP_SYS_ADMIN))
		return -EPERM;

	kattr->mnt_userns = get_user_ns(mnt_userns);
	return 0;
}

static int build_mount_kattr(const struct mount_attr *attr, size_t usize,
			     struct mount_kattr *kattr)
{
	if (attr->propagation & ~MOUNT_SETATTR_PROPAGATION_FLAGS)
		return -EINVAL;
	if (hweight32(attr->propagation & MOUNT_SETATTR_PROPAGATION_FLAGS) > 1)
		return -EINVAL;
	kattr->propagation = attr->propagation;

	if ((attr->attr_set | attr->attr_clr) & ~MOUNT_SETATTR_VALID_FLAGS)
		return -EINVAL;

	kattr->attr_set = attr_flags_to_mnt_flags(attr->attr_set);
	kattr->attr_clr = attr_flags_to_mnt_flags(attr->attr_clr);

	/*
	 * Since the MOUNT_ATTR_<atime> values are an enum, not a bitmap,
	 * users wanting to transition to a different atime setting cannot
	 * simply specify the atime setting in @attr_set, but must also
	 * specify MOUNT_ATTR__ATIME in the @attr_clr field.
	 * So ensure that MOUNT_ATTR__ATIME can't be partially set in
	 * @attr_clr and that @attr_set can't have any atime bits set if
	 * MOUNT_ATTR__ATIME isn't set in @attr_clr.
	 */
	if (attr->attr_clr & MOUNT_ATTR__ATIME) {
		if ((attr->attr_clr & MOUNT_ATTR__ATIME) != MOUNT_ATTR__ATIME)
			return -EINVAL;

		/*
		 * Clear all previous time settings as they are mutually
		 * exclusive.
		 */
		kattr->attr_clr |= MNT_RELATIME | MNT_NOATIME;
		switch (attr->attr_set & MOUNT_ATTR__ATIME) {
		case MOUNT_ATTR_RELATIME:
			kattr->attr_set |= MNT_RELATIME;
			break;
		case MOUNT_ATTR_NOATIME:
			kattr->attr_set |= MNT_NOATIME;
			break;
		case MOUNT_ATTR_STRICTATIME:
			break;
		default:
			return -EINVAL;
		}
	} else {
		if (attr->attr_set & MOUNT_ATTR__ATIME)
			return -EINVAL;
	}

	return build_mount_idmapped(attr, usize, kattr);
}

static void finish_mount_kattr(struct mount_kattr *kattr)
{
	if (kattr->mnt_userns) {
		put_user_ns(kattr->mnt_userns);
		kattr->mnt_userns = NULL;
	}

	if (kattr->mnt_idmap)
		mnt_idmap_put(kattr->mnt_idmap);
}

static int wants_mount_setattr(struct mount_attr __user *uattr, size_t usize,
			       struct mount_kattr *kattr)
{
	int ret;
	struct mount_attr attr;

	BUILD_BUG_ON(sizeof(struct mount_attr) != MOUNT_ATTR_SIZE_VER0);

	if (unlikely(usize > PAGE_SIZE))
		return -E2BIG;
	if (unlikely(usize < MOUNT_ATTR_SIZE_VER0))
		return -EINVAL;

	if (!may_mount())
		return -EPERM;

	ret = copy_struct_from_user(&attr, sizeof(attr), uattr, usize);
	if (ret)
		return ret;

	/* Don't bother walking through the mounts if this is a nop. */
	if (attr.attr_set == 0 &&
	    attr.attr_clr == 0 &&
	    attr.propagation == 0)
		return 0; /* Tell caller to not bother. */

	ret = build_mount_kattr(&attr, usize, kattr);
	if (ret < 0)
		return ret;

	return 1;
}

SYSCALL_DEFINE5(mount_setattr, int, dfd, const char __user *, path,
		unsigned int, flags, struct mount_attr __user *, uattr,
		size_t, usize)
{
	int err;
	struct path target;
	struct mount_kattr kattr;
	unsigned int lookup_flags = LOOKUP_AUTOMOUNT | LOOKUP_FOLLOW;

	if (flags & ~(AT_EMPTY_PATH |
		      AT_RECURSIVE |
		      AT_SYMLINK_NOFOLLOW |
		      AT_NO_AUTOMOUNT))
		return -EINVAL;

	if (flags & AT_NO_AUTOMOUNT)
		lookup_flags &= ~LOOKUP_AUTOMOUNT;
	if (flags & AT_SYMLINK_NOFOLLOW)
		lookup_flags &= ~LOOKUP_FOLLOW;
	if (flags & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;

	kattr = (struct mount_kattr) {
		.lookup_flags	= lookup_flags,
	};

	if (flags & AT_RECURSIVE)
		kattr.kflags |= MOUNT_KATTR_RECURSE;

	err = wants_mount_setattr(uattr, usize, &kattr);
	if (err <= 0)
		return err;

	err = user_path_at(dfd, path, kattr.lookup_flags, &target);
	if (!err) {
		err = do_mount_setattr(&target, &kattr);
		path_put(&target);
	}
	finish_mount_kattr(&kattr);
	return err;
}

SYSCALL_DEFINE5(open_tree_attr, int, dfd, const char __user *, filename,
		unsigned, flags, struct mount_attr __user *, uattr,
		size_t, usize)
{
	struct file __free(fput) *file = NULL;
	int fd;

	if (!uattr && usize)
		return -EINVAL;

	file = vfs_open_tree(dfd, filename, flags);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (uattr) {
		int ret;
		struct mount_kattr kattr = {};

		kattr.kflags = MOUNT_KATTR_IDMAP_REPLACE;
		if (flags & AT_RECURSIVE)
			kattr.kflags |= MOUNT_KATTR_RECURSE;

		ret = wants_mount_setattr(uattr, usize, &kattr);
		if (ret > 0) {
			ret = do_mount_setattr(&file->f_path, &kattr);
			finish_mount_kattr(&kattr);
		}
		if (ret)
			return ret;
	}

	fd = get_unused_fd_flags(flags & O_CLOEXEC);
	if (fd < 0)
		return fd;

	fd_install(fd, no_free_ptr(file));
	return fd;
}

int show_path(struct seq_file *m, struct dentry *root)
{
	if (root->d_sb->s_op->show_path)
		return root->d_sb->s_op->show_path(m, root);

	seq_dentry(m, root, " \t\n\\");
	return 0;
}

static struct vfsmount *lookup_mnt_in_ns(u64 id, struct mnt_namespace *ns)
{
	struct mount *mnt = mnt_find_id_at(ns, id);

	if (!mnt || mnt->mnt_id_unique != id)
		return NULL;

	return &mnt->mnt;
}

struct kstatmount {
	struct statmount __user *buf;
	size_t bufsize;
	struct vfsmount *mnt;
	struct mnt_idmap *idmap;
	u64 mask;
	struct path root;
	struct seq_file seq;

	/* Must be last --ends in a flexible-array member. */
	struct statmount sm;
};

static u64 mnt_to_attr_flags(struct vfsmount *mnt)
{
	unsigned int mnt_flags = READ_ONCE(mnt->mnt_flags);
	u64 attr_flags = 0;

	if (mnt_flags & MNT_READONLY)
		attr_flags |= MOUNT_ATTR_RDONLY;
	if (mnt_flags & MNT_NOSUID)
		attr_flags |= MOUNT_ATTR_NOSUID;
	if (mnt_flags & MNT_NODEV)
		attr_flags |= MOUNT_ATTR_NODEV;
	if (mnt_flags & MNT_NOEXEC)
		attr_flags |= MOUNT_ATTR_NOEXEC;
	if (mnt_flags & MNT_NODIRATIME)
		attr_flags |= MOUNT_ATTR_NODIRATIME;
	if (mnt_flags & MNT_NOSYMFOLLOW)
		attr_flags |= MOUNT_ATTR_NOSYMFOLLOW;

	if (mnt_flags & MNT_NOATIME)
		attr_flags |= MOUNT_ATTR_NOATIME;
	else if (mnt_flags & MNT_RELATIME)
		attr_flags |= MOUNT_ATTR_RELATIME;
	else
		attr_flags |= MOUNT_ATTR_STRICTATIME;

	if (is_idmapped_mnt(mnt))
		attr_flags |= MOUNT_ATTR_IDMAP;

	return attr_flags;
}

static u64 mnt_to_propagation_flags(struct mount *m)
{
	u64 propagation = 0;

	if (IS_MNT_SHARED(m))
		propagation |= MS_SHARED;
	if (IS_MNT_SLAVE(m))
		propagation |= MS_SLAVE;
	if (IS_MNT_UNBINDABLE(m))
		propagation |= MS_UNBINDABLE;
	if (!propagation)
		propagation |= MS_PRIVATE;

	return propagation;
}

static void statmount_sb_basic(struct kstatmount *s)
{
	struct super_block *sb = s->mnt->mnt_sb;

	s->sm.mask |= STATMOUNT_SB_BASIC;
	s->sm.sb_dev_major = MAJOR(sb->s_dev);
	s->sm.sb_dev_minor = MINOR(sb->s_dev);
	s->sm.sb_magic = sb->s_magic;
	s->sm.sb_flags = sb->s_flags & (SB_RDONLY|SB_SYNCHRONOUS|SB_DIRSYNC|SB_LAZYTIME);
}

static void statmount_mnt_basic(struct kstatmount *s)
{
	struct mount *m = real_mount(s->mnt);

	s->sm.mask |= STATMOUNT_MNT_BASIC;
	s->sm.mnt_id = m->mnt_id_unique;
	s->sm.mnt_parent_id = m->mnt_parent->mnt_id_unique;
	s->sm.mnt_id_old = m->mnt_id;
	s->sm.mnt_parent_id_old = m->mnt_parent->mnt_id;
	s->sm.mnt_attr = mnt_to_attr_flags(&m->mnt);
	s->sm.mnt_propagation = mnt_to_propagation_flags(m);
	s->sm.mnt_peer_group = m->mnt_group_id;
	s->sm.mnt_master = IS_MNT_SLAVE(m) ? m->mnt_master->mnt_group_id : 0;
}

static void statmount_propagate_from(struct kstatmount *s)
{
	struct mount *m = real_mount(s->mnt);

	s->sm.mask |= STATMOUNT_PROPAGATE_FROM;
	if (IS_MNT_SLAVE(m))
		s->sm.propagate_from = get_dominating_id(m, &current->fs->root);
}

static int statmount_mnt_root(struct kstatmount *s, struct seq_file *seq)
{
	int ret;
	size_t start = seq->count;

	ret = show_path(seq, s->mnt->mnt_root);
	if (ret)
		return ret;

	if (unlikely(seq_has_overflowed(seq)))
		return -EAGAIN;

	/*
         * Unescape the result. It would be better if supplied string was not
         * escaped in the first place, but that's a pretty invasive change.
         */
	seq->buf[seq->count] = '\0';
	seq->count = start;
	seq_commit(seq, string_unescape_inplace(seq->buf + start, UNESCAPE_OCTAL));
	return 0;
}

static int statmount_mnt_point(struct kstatmount *s, struct seq_file *seq)
{
	struct vfsmount *mnt = s->mnt;
	struct path mnt_path = { .dentry = mnt->mnt_root, .mnt = mnt };
	int err;

	err = seq_path_root(seq, &mnt_path, &s->root, "");
	return err == SEQ_SKIP ? 0 : err;
}

static int statmount_fs_type(struct kstatmount *s, struct seq_file *seq)
{
	struct super_block *sb = s->mnt->mnt_sb;

	seq_puts(seq, sb->s_type->name);
	return 0;
}

static void statmount_fs_subtype(struct kstatmount *s, struct seq_file *seq)
{
	struct super_block *sb = s->mnt->mnt_sb;

	if (sb->s_subtype)
		seq_puts(seq, sb->s_subtype);
}

static int statmount_sb_source(struct kstatmount *s, struct seq_file *seq)
{
	struct super_block *sb = s->mnt->mnt_sb;
	struct mount *r = real_mount(s->mnt);

	if (sb->s_op->show_devname) {
		size_t start = seq->count;
		int ret;

		ret = sb->s_op->show_devname(seq, s->mnt->mnt_root);
		if (ret)
			return ret;

		if (unlikely(seq_has_overflowed(seq)))
			return -EAGAIN;

		/* Unescape the result */
		seq->buf[seq->count] = '\0';
		seq->count = start;
		seq_commit(seq, string_unescape_inplace(seq->buf + start, UNESCAPE_OCTAL));
	} else {
		seq_puts(seq, r->mnt_devname);
	}
	return 0;
}

static void statmount_mnt_ns_id(struct kstatmount *s, struct mnt_namespace *ns)
{
	s->sm.mask |= STATMOUNT_MNT_NS_ID;
	s->sm.mnt_ns_id = ns->seq;
}

static int statmount_mnt_opts(struct kstatmount *s, struct seq_file *seq)
{
	struct vfsmount *mnt = s->mnt;
	struct super_block *sb = mnt->mnt_sb;
	size_t start = seq->count;
	int err;

	err = security_sb_show_options(seq, sb);
	if (err)
		return err;

	if (sb->s_op->show_options) {
		err = sb->s_op->show_options(seq, mnt->mnt_root);
		if (err)
			return err;
	}

	if (unlikely(seq_has_overflowed(seq)))
		return -EAGAIN;

	if (seq->count == start)
		return 0;

	/* skip leading comma */
	memmove(seq->buf + start, seq->buf + start + 1,
		seq->count - start - 1);
	seq->count--;

	return 0;
}

static inline int statmount_opt_process(struct seq_file *seq, size_t start)
{
	char *buf_end, *opt_end, *src, *dst;
	int count = 0;

	if (unlikely(seq_has_overflowed(seq)))
		return -EAGAIN;

	buf_end = seq->buf + seq->count;
	dst = seq->buf + start;
	src = dst + 1;	/* skip initial comma */

	if (src >= buf_end) {
		seq->count = start;
		return 0;
	}

	*buf_end = '\0';
	for (; src < buf_end; src = opt_end + 1) {
		opt_end = strchrnul(src, ',');
		*opt_end = '\0';
		dst += string_unescape(src, dst, 0, UNESCAPE_OCTAL) + 1;
		if (WARN_ON_ONCE(++count == INT_MAX))
			return -EOVERFLOW;
	}
	seq->count = dst - 1 - seq->buf;
	return count;
}

static int statmount_opt_array(struct kstatmount *s, struct seq_file *seq)
{
	struct vfsmount *mnt = s->mnt;
	struct super_block *sb = mnt->mnt_sb;
	size_t start = seq->count;
	int err;

	if (!sb->s_op->show_options)
		return 0;

	err = sb->s_op->show_options(seq, mnt->mnt_root);
	if (err)
		return err;

	err = statmount_opt_process(seq, start);
	if (err < 0)
		return err;

	s->sm.opt_num = err;
	return 0;
}

static int statmount_opt_sec_array(struct kstatmount *s, struct seq_file *seq)
{
	struct vfsmount *mnt = s->mnt;
	struct super_block *sb = mnt->mnt_sb;
	size_t start = seq->count;
	int err;

	err = security_sb_show_options(seq, sb);
	if (err)
		return err;

	err = statmount_opt_process(seq, start);
	if (err < 0)
		return err;

	s->sm.opt_sec_num = err;
	return 0;
}

static inline int statmount_mnt_uidmap(struct kstatmount *s, struct seq_file *seq)
{
	int ret;

	ret = statmount_mnt_idmap(s->idmap, seq, true);
	if (ret < 0)
		return ret;

	s->sm.mnt_uidmap_num = ret;
	/*
	 * Always raise STATMOUNT_MNT_UIDMAP even if there are no valid
	 * mappings. This allows userspace to distinguish between a
	 * non-idmapped mount and an idmapped mount where none of the
	 * individual mappings are valid in the caller's idmapping.
	 */
	if (is_valid_mnt_idmap(s->idmap))
		s->sm.mask |= STATMOUNT_MNT_UIDMAP;
	return 0;
}

static inline int statmount_mnt_gidmap(struct kstatmount *s, struct seq_file *seq)
{
	int ret;

	ret = statmount_mnt_idmap(s->idmap, seq, false);
	if (ret < 0)
		return ret;

	s->sm.mnt_gidmap_num = ret;
	/*
	 * Always raise STATMOUNT_MNT_GIDMAP even if there are no valid
	 * mappings. This allows userspace to distinguish between a
	 * non-idmapped mount and an idmapped mount where none of the
	 * individual mappings are valid in the caller's idmapping.
	 */
	if (is_valid_mnt_idmap(s->idmap))
		s->sm.mask |= STATMOUNT_MNT_GIDMAP;
	return 0;
}

static int statmount_string(struct kstatmount *s, u64 flag)
{
	int ret = 0;
	size_t kbufsize;
	struct seq_file *seq = &s->seq;
	struct statmount *sm = &s->sm;
	u32 start, *offp;

	/* Reserve an empty string at the beginning for any unset offsets */
	if (!seq->count)
		seq_putc(seq, 0);

	start = seq->count;

	switch (flag) {
	case STATMOUNT_FS_TYPE:
		offp = &sm->fs_type;
		ret = statmount_fs_type(s, seq);
		break;
	case STATMOUNT_MNT_ROOT:
		offp = &sm->mnt_root;
		ret = statmount_mnt_root(s, seq);
		break;
	case STATMOUNT_MNT_POINT:
		offp = &sm->mnt_point;
		ret = statmount_mnt_point(s, seq);
		break;
	case STATMOUNT_MNT_OPTS:
		offp = &sm->mnt_opts;
		ret = statmount_mnt_opts(s, seq);
		break;
	case STATMOUNT_OPT_ARRAY:
		offp = &sm->opt_array;
		ret = statmount_opt_array(s, seq);
		break;
	case STATMOUNT_OPT_SEC_ARRAY:
		offp = &sm->opt_sec_array;
		ret = statmount_opt_sec_array(s, seq);
		break;
	case STATMOUNT_FS_SUBTYPE:
		offp = &sm->fs_subtype;
		statmount_fs_subtype(s, seq);
		break;
	case STATMOUNT_SB_SOURCE:
		offp = &sm->sb_source;
		ret = statmount_sb_source(s, seq);
		break;
	case STATMOUNT_MNT_UIDMAP:
		sm->mnt_uidmap = start;
		ret = statmount_mnt_uidmap(s, seq);
		break;
	case STATMOUNT_MNT_GIDMAP:
		sm->mnt_gidmap = start;
		ret = statmount_mnt_gidmap(s, seq);
		break;
	default:
		WARN_ON_ONCE(true);
		return -EINVAL;
	}

	/*
	 * If nothing was emitted, return to avoid setting the flag
	 * and terminating the buffer.
	 */
	if (seq->count == start)
		return ret;
	if (unlikely(check_add_overflow(sizeof(*sm), seq->count, &kbufsize)))
		return -EOVERFLOW;
	if (kbufsize >= s->bufsize)
		return -EOVERFLOW;

	/* signal a retry */
	if (unlikely(seq_has_overflowed(seq)))
		return -EAGAIN;

	if (ret)
		return ret;

	seq->buf[seq->count++] = '\0';
	sm->mask |= flag;
	*offp = start;
	return 0;
}

static int copy_statmount_to_user(struct kstatmount *s)
{
	struct statmount *sm = &s->sm;
	struct seq_file *seq = &s->seq;
	char __user *str = ((char __user *)s->buf) + sizeof(*sm);
	size_t copysize = min_t(size_t, s->bufsize, sizeof(*sm));

	if (seq->count && copy_to_user(str, seq->buf, seq->count))
		return -EFAULT;

	/* Return the number of bytes copied to the buffer */
	sm->size = copysize + seq->count;
	if (copy_to_user(s->buf, sm, copysize))
		return -EFAULT;

	return 0;
}

static struct mount *listmnt_next(struct mount *curr, bool reverse)
{
	struct rb_node *node;

	if (reverse)
		node = rb_prev(&curr->mnt_node);
	else
		node = rb_next(&curr->mnt_node);

	return node_to_mount(node);
}

static int grab_requested_root(struct mnt_namespace *ns, struct path *root)
{
	struct mount *first, *child;

	rwsem_assert_held(&namespace_sem);

	/* We're looking at our own ns, just use get_fs_root. */
	if (ns == current->nsproxy->mnt_ns) {
		get_fs_root(current->fs, root);
		return 0;
	}

	/*
	 * We have to find the first mount in our ns and use that, however it
	 * may not exist, so handle that properly.
	 */
	if (mnt_ns_empty(ns))
		return -ENOENT;

	first = child = ns->root;
	for (;;) {
		child = listmnt_next(child, false);
		if (!child)
			return -ENOENT;
		if (child->mnt_parent == first)
			break;
	}

	root->mnt = mntget(&child->mnt);
	root->dentry = dget(root->mnt->mnt_root);
	return 0;
}

/* This must be updated whenever a new flag is added */
#define STATMOUNT_SUPPORTED (STATMOUNT_SB_BASIC | \
			     STATMOUNT_MNT_BASIC | \
			     STATMOUNT_PROPAGATE_FROM | \
			     STATMOUNT_MNT_ROOT | \
			     STATMOUNT_MNT_POINT | \
			     STATMOUNT_FS_TYPE | \
			     STATMOUNT_MNT_NS_ID | \
			     STATMOUNT_MNT_OPTS | \
			     STATMOUNT_FS_SUBTYPE | \
			     STATMOUNT_SB_SOURCE | \
			     STATMOUNT_OPT_ARRAY | \
			     STATMOUNT_OPT_SEC_ARRAY | \
			     STATMOUNT_SUPPORTED_MASK | \
			     STATMOUNT_MNT_UIDMAP | \
			     STATMOUNT_MNT_GIDMAP)

static int do_statmount(struct kstatmount *s, u64 mnt_id, u64 mnt_ns_id,
			struct mnt_namespace *ns)
{
	struct path root __free(path_put) = {};
	struct mount *m;
	int err;

	/* Has the namespace already been emptied? */
	if (mnt_ns_id && mnt_ns_empty(ns))
		return -ENOENT;

	s->mnt = lookup_mnt_in_ns(mnt_id, ns);
	if (!s->mnt)
		return -ENOENT;

	err = grab_requested_root(ns, &root);
	if (err)
		return err;

	/*
	 * Don't trigger audit denials. We just want to determine what
	 * mounts to show users.
	 */
	m = real_mount(s->mnt);
	if (!is_path_reachable(m, m->mnt.mnt_root, &root) &&
	    !ns_capable_noaudit(ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	err = security_sb_statfs(s->mnt->mnt_root);
	if (err)
		return err;

	s->root = root;

	/*
	 * Note that mount properties in mnt->mnt_flags, mnt->mnt_idmap
	 * can change concurrently as we only hold the read-side of the
	 * namespace semaphore and mount properties may change with only
	 * the mount lock held.
	 *
	 * We could sample the mount lock sequence counter to detect
	 * those changes and retry. But it's not worth it. Worst that
	 * happens is that the mnt->mnt_idmap pointer is already changed
	 * while mnt->mnt_flags isn't or vica versa. So what.
	 *
	 * Both mnt->mnt_flags and mnt->mnt_idmap are set and retrieved
	 * via READ_ONCE()/WRITE_ONCE() and guard against theoretical
	 * torn read/write. That's all we care about right now.
	 */
	s->idmap = mnt_idmap(s->mnt);
	if (s->mask & STATMOUNT_MNT_BASIC)
		statmount_mnt_basic(s);

	if (s->mask & STATMOUNT_SB_BASIC)
		statmount_sb_basic(s);

	if (s->mask & STATMOUNT_PROPAGATE_FROM)
		statmount_propagate_from(s);

	if (s->mask & STATMOUNT_FS_TYPE)
		err = statmount_string(s, STATMOUNT_FS_TYPE);

	if (!err && s->mask & STATMOUNT_MNT_ROOT)
		err = statmount_string(s, STATMOUNT_MNT_ROOT);

	if (!err && s->mask & STATMOUNT_MNT_POINT)
		err = statmount_string(s, STATMOUNT_MNT_POINT);

	if (!err && s->mask & STATMOUNT_MNT_OPTS)
		err = statmount_string(s, STATMOUNT_MNT_OPTS);

	if (!err && s->mask & STATMOUNT_OPT_ARRAY)
		err = statmount_string(s, STATMOUNT_OPT_ARRAY);

	if (!err && s->mask & STATMOUNT_OPT_SEC_ARRAY)
		err = statmount_string(s, STATMOUNT_OPT_SEC_ARRAY);

	if (!err && s->mask & STATMOUNT_FS_SUBTYPE)
		err = statmount_string(s, STATMOUNT_FS_SUBTYPE);

	if (!err && s->mask & STATMOUNT_SB_SOURCE)
		err = statmount_string(s, STATMOUNT_SB_SOURCE);

	if (!err && s->mask & STATMOUNT_MNT_UIDMAP)
		err = statmount_string(s, STATMOUNT_MNT_UIDMAP);

	if (!err && s->mask & STATMOUNT_MNT_GIDMAP)
		err = statmount_string(s, STATMOUNT_MNT_GIDMAP);

	if (!err && s->mask & STATMOUNT_MNT_NS_ID)
		statmount_mnt_ns_id(s, ns);

	if (!err && s->mask & STATMOUNT_SUPPORTED_MASK) {
		s->sm.mask |= STATMOUNT_SUPPORTED_MASK;
		s->sm.supported_mask = STATMOUNT_SUPPORTED;
	}

	if (err)
		return err;

	/* Are there bits in the return mask not present in STATMOUNT_SUPPORTED? */
	WARN_ON_ONCE(~STATMOUNT_SUPPORTED & s->sm.mask);

	return 0;
}

static inline bool retry_statmount(const long ret, size_t *seq_size)
{
	if (likely(ret != -EAGAIN))
		return false;
	if (unlikely(check_mul_overflow(*seq_size, 2, seq_size)))
		return false;
	if (unlikely(*seq_size > MAX_RW_COUNT))
		return false;
	return true;
}

#define STATMOUNT_STRING_REQ (STATMOUNT_MNT_ROOT | STATMOUNT_MNT_POINT | \
			      STATMOUNT_FS_TYPE | STATMOUNT_MNT_OPTS | \
			      STATMOUNT_FS_SUBTYPE | STATMOUNT_SB_SOURCE | \
			      STATMOUNT_OPT_ARRAY | STATMOUNT_OPT_SEC_ARRAY | \
			      STATMOUNT_MNT_UIDMAP | STATMOUNT_MNT_GIDMAP)

static int prepare_kstatmount(struct kstatmount *ks, struct mnt_id_req *kreq,
			      struct statmount __user *buf, size_t bufsize,
			      size_t seq_size)
{
	if (!access_ok(buf, bufsize))
		return -EFAULT;

	memset(ks, 0, sizeof(*ks));
	ks->mask = kreq->param;
	ks->buf = buf;
	ks->bufsize = bufsize;

	if (ks->mask & STATMOUNT_STRING_REQ) {
		if (bufsize == sizeof(ks->sm))
			return -EOVERFLOW;

		ks->seq.buf = kvmalloc(seq_size, GFP_KERNEL_ACCOUNT);
		if (!ks->seq.buf)
			return -ENOMEM;

		ks->seq.size = seq_size;
	}

	return 0;
}

static int copy_mnt_id_req(const struct mnt_id_req __user *req,
			   struct mnt_id_req *kreq)
{
	int ret;
	size_t usize;

	BUILD_BUG_ON(sizeof(struct mnt_id_req) != MNT_ID_REQ_SIZE_VER1);

	ret = get_user(usize, &req->size);
	if (ret)
		return -EFAULT;
	if (unlikely(usize > PAGE_SIZE))
		return -E2BIG;
	if (unlikely(usize < MNT_ID_REQ_SIZE_VER0))
		return -EINVAL;
	memset(kreq, 0, sizeof(*kreq));
	ret = copy_struct_from_user(kreq, sizeof(*kreq), req, usize);
	if (ret)
		return ret;
	if (kreq->spare != 0)
		return -EINVAL;
	/* The first valid unique mount id is MNT_UNIQUE_ID_OFFSET + 1. */
	if (kreq->mnt_id <= MNT_UNIQUE_ID_OFFSET)
		return -EINVAL;
	return 0;
}

/*
 * If the user requested a specific mount namespace id, look that up and return
 * that, or if not simply grab a passive reference on our mount namespace and
 * return that.
 */
static struct mnt_namespace *grab_requested_mnt_ns(const struct mnt_id_req *kreq)
{
	struct mnt_namespace *mnt_ns;

	if (kreq->mnt_ns_id && kreq->spare)
		return ERR_PTR(-EINVAL);

	if (kreq->mnt_ns_id)
		return lookup_mnt_ns(kreq->mnt_ns_id);

	if (kreq->spare) {
		struct ns_common *ns;

		CLASS(fd, f)(kreq->spare);
		if (fd_empty(f))
			return ERR_PTR(-EBADF);

		if (!proc_ns_file(fd_file(f)))
			return ERR_PTR(-EINVAL);

		ns = get_proc_ns(file_inode(fd_file(f)));
		if (ns->ops->type != CLONE_NEWNS)
			return ERR_PTR(-EINVAL);

		mnt_ns = to_mnt_ns(ns);
	} else {
		mnt_ns = current->nsproxy->mnt_ns;
	}

	refcount_inc(&mnt_ns->passive);
	return mnt_ns;
}

SYSCALL_DEFINE4(statmount, const struct mnt_id_req __user *, req,
		struct statmount __user *, buf, size_t, bufsize,
		unsigned int, flags)
{
	struct mnt_namespace *ns __free(mnt_ns_release) = NULL;
	struct kstatmount *ks __free(kfree) = NULL;
	struct mnt_id_req kreq;
	/* We currently support retrieval of 3 strings. */
	size_t seq_size = 3 * PATH_MAX;
	int ret;

	if (flags)
		return -EINVAL;

	ret = copy_mnt_id_req(req, &kreq);
	if (ret)
		return ret;

	ns = grab_requested_mnt_ns(&kreq);
	if (!ns)
		return -ENOENT;

	if (kreq.mnt_ns_id && (ns != current->nsproxy->mnt_ns) &&
	    !ns_capable_noaudit(ns->user_ns, CAP_SYS_ADMIN))
		return -ENOENT;

	ks = kmalloc(sizeof(*ks), GFP_KERNEL_ACCOUNT);
	if (!ks)
		return -ENOMEM;

retry:
	ret = prepare_kstatmount(ks, &kreq, buf, bufsize, seq_size);
	if (ret)
		return ret;

	scoped_guard(rwsem_read, &namespace_sem)
		ret = do_statmount(ks, kreq.mnt_id, kreq.mnt_ns_id, ns);

	if (!ret)
		ret = copy_statmount_to_user(ks);
	kvfree(ks->seq.buf);
	if (retry_statmount(ret, &seq_size))
		goto retry;
	return ret;
}

static ssize_t do_listmount(struct mnt_namespace *ns, u64 mnt_parent_id,
			    u64 last_mnt_id, u64 *mnt_ids, size_t nr_mnt_ids,
			    bool reverse)
{
	struct path root __free(path_put) = {};
	struct path orig;
	struct mount *r, *first;
	ssize_t ret;

	rwsem_assert_held(&namespace_sem);

	ret = grab_requested_root(ns, &root);
	if (ret)
		return ret;

	if (mnt_parent_id == LSMT_ROOT) {
		orig = root;
	} else {
		orig.mnt = lookup_mnt_in_ns(mnt_parent_id, ns);
		if (!orig.mnt)
			return -ENOENT;
		orig.dentry = orig.mnt->mnt_root;
	}

	/*
	 * Don't trigger audit denials. We just want to determine what
	 * mounts to show users.
	 */
	if (!is_path_reachable(real_mount(orig.mnt), orig.dentry, &root) &&
	    !ns_capable_noaudit(ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	ret = security_sb_statfs(orig.dentry);
	if (ret)
		return ret;

	if (!last_mnt_id) {
		if (reverse)
			first = node_to_mount(ns->mnt_last_node);
		else
			first = node_to_mount(ns->mnt_first_node);
	} else {
		if (reverse)
			first = mnt_find_id_at_reverse(ns, last_mnt_id - 1);
		else
			first = mnt_find_id_at(ns, last_mnt_id + 1);
	}

	for (ret = 0, r = first; r && nr_mnt_ids; r = listmnt_next(r, reverse)) {
		if (r->mnt_id_unique == mnt_parent_id)
			continue;
		if (!is_path_reachable(r, r->mnt.mnt_root, &orig))
			continue;
		*mnt_ids = r->mnt_id_unique;
		mnt_ids++;
		nr_mnt_ids--;
		ret++;
	}
	return ret;
}

SYSCALL_DEFINE4(listmount, const struct mnt_id_req __user *, req,
		u64 __user *, mnt_ids, size_t, nr_mnt_ids, unsigned int, flags)
{
	u64 *kmnt_ids __free(kvfree) = NULL;
	const size_t maxcount = 1000000;
	struct mnt_namespace *ns __free(mnt_ns_release) = NULL;
	struct mnt_id_req kreq;
	u64 last_mnt_id;
	ssize_t ret;

	if (flags & ~LISTMOUNT_REVERSE)
		return -EINVAL;

	/*
	 * If the mount namespace really has more than 1 million mounts the
	 * caller must iterate over the mount namespace (and reconsider their
	 * system design...).
	 */
	if (unlikely(nr_mnt_ids > maxcount))
		return -EOVERFLOW;

	if (!access_ok(mnt_ids, nr_mnt_ids * sizeof(*mnt_ids)))
		return -EFAULT;

	ret = copy_mnt_id_req(req, &kreq);
	if (ret)
		return ret;

	last_mnt_id = kreq.param;
	/* The first valid unique mount id is MNT_UNIQUE_ID_OFFSET + 1. */
	if (last_mnt_id != 0 && last_mnt_id <= MNT_UNIQUE_ID_OFFSET)
		return -EINVAL;

	kmnt_ids = kvmalloc_array(nr_mnt_ids, sizeof(*kmnt_ids),
				  GFP_KERNEL_ACCOUNT);
	if (!kmnt_ids)
		return -ENOMEM;

	ns = grab_requested_mnt_ns(&kreq);
	if (!ns)
		return -ENOENT;

	if (kreq.mnt_ns_id && (ns != current->nsproxy->mnt_ns) &&
	    !ns_capable_noaudit(ns->user_ns, CAP_SYS_ADMIN))
		return -ENOENT;

	/*
	 * We only need to guard against mount topology changes as
	 * listmount() doesn't care about any mount properties.
	 */
	scoped_guard(rwsem_read, &namespace_sem)
		ret = do_listmount(ns, kreq.mnt_id, last_mnt_id, kmnt_ids,
				   nr_mnt_ids, (flags & LISTMOUNT_REVERSE));
	if (ret <= 0)
		return ret;

	if (copy_to_user(mnt_ids, kmnt_ids, ret * sizeof(*mnt_ids)))
		return -EFAULT;

	return ret;
}

static void __init init_mount_tree(void)
{
	struct vfsmount *mnt;
	struct mount *m;
	struct mnt_namespace *ns;
	struct path root;

	mnt = vfs_kern_mount(&rootfs_fs_type, 0, "rootfs", NULL);
	if (IS_ERR(mnt))
		panic("Can't create rootfs");

	ns = alloc_mnt_ns(&init_user_ns, true);
	if (IS_ERR(ns))
		panic("Can't allocate initial namespace");
	ns->seq = atomic64_inc_return(&mnt_ns_seq);
	ns->ns.inum = PROC_MNT_INIT_INO;
	m = real_mount(mnt);
	ns->root = m;
	ns->nr_mounts = 1;
	mnt_add_to_ns(ns, m);
	init_task.nsproxy->mnt_ns = ns;
	get_mnt_ns(ns);

	root.mnt = mnt;
	root.dentry = mnt->mnt_root;

	set_fs_pwd(current->fs, &root);
	set_fs_root(current->fs, &root);

	mnt_ns_tree_add(ns);
}

void __init mnt_init(void)
{
	int err;

	mnt_cache = kmem_cache_create("mnt_cache", sizeof(struct mount),
			0, SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT, NULL);

	mount_hashtable = alloc_large_system_hash("Mount-cache",
				sizeof(struct hlist_head),
				mhash_entries, 19,
				HASH_ZERO,
				&m_hash_shift, &m_hash_mask, 0, 0);
	mountpoint_hashtable = alloc_large_system_hash("Mountpoint-cache",
				sizeof(struct hlist_head),
				mphash_entries, 19,
				HASH_ZERO,
				&mp_hash_shift, &mp_hash_mask, 0, 0);

	if (!mount_hashtable || !mountpoint_hashtable)
		panic("Failed to allocate mount hash table\n");

	kernfs_init();

	err = sysfs_init();
	if (err)
		printk(KERN_WARNING "%s: sysfs_init error: %d\n",
			__func__, err);
	fs_kobj = kobject_create_and_add("fs", NULL);
	if (!fs_kobj)
		printk(KERN_WARNING "%s: kobj create error\n", __func__);
	shmem_init();
	init_rootfs();
	init_mount_tree();
}

void put_mnt_ns(struct mnt_namespace *ns)
{
	if (!refcount_dec_and_test(&ns->ns.count))
		return;
	namespace_lock();
	emptied_ns = ns;
	lock_mount_hash();
	umount_tree(ns->root, 0);
	unlock_mount_hash();
	namespace_unlock();
}

struct vfsmount *kern_mount(struct file_system_type *type)
{
	struct vfsmount *mnt;
	mnt = vfs_kern_mount(type, SB_KERNMOUNT, type->name, NULL);
	if (!IS_ERR(mnt)) {
		/*
		 * it is a longterm mount, don't release mnt until
		 * we unmount before file sys is unregistered
		*/
		real_mount(mnt)->mnt_ns = MNT_NS_INTERNAL;
	}
	return mnt;
}
EXPORT_SYMBOL_GPL(kern_mount);

void kern_unmount(struct vfsmount *mnt)
{
	/* release long term mount so mount point can be released */
	if (!IS_ERR(mnt)) {
		mnt_make_shortterm(mnt);
		synchronize_rcu();	/* yecchhh... */
		mntput(mnt);
	}
}
EXPORT_SYMBOL(kern_unmount);

void kern_unmount_array(struct vfsmount *mnt[], unsigned int num)
{
	unsigned int i;

	for (i = 0; i < num; i++)
		mnt_make_shortterm(mnt[i]);
	synchronize_rcu_expedited();
	for (i = 0; i < num; i++)
		mntput(mnt[i]);
}
EXPORT_SYMBOL(kern_unmount_array);

bool our_mnt(struct vfsmount *mnt)
{
	return check_mnt(real_mount(mnt));
}

bool current_chrooted(void)
{
	/* Does the current process have a non-standard root */
	struct path ns_root;
	struct path fs_root;
	bool chrooted;

	/* Find the namespace root */
	ns_root.mnt = &current->nsproxy->mnt_ns->root->mnt;
	ns_root.dentry = ns_root.mnt->mnt_root;
	path_get(&ns_root);
	while (d_mountpoint(ns_root.dentry) && follow_down_one(&ns_root))
		;

	get_fs_root(current->fs, &fs_root);

	chrooted = !path_equal(&fs_root, &ns_root);

	path_put(&fs_root);
	path_put(&ns_root);

	return chrooted;
}

static bool mnt_already_visible(struct mnt_namespace *ns,
				const struct super_block *sb,
				int *new_mnt_flags)
{
	int new_flags = *new_mnt_flags;
	struct mount *mnt, *n;
	bool visible = false;

	down_read(&namespace_sem);
	rbtree_postorder_for_each_entry_safe(mnt, n, &ns->mounts, mnt_node) {
		struct mount *child;
		int mnt_flags;

		if (mnt->mnt.mnt_sb->s_type != sb->s_type)
			continue;

		/* This mount is not fully visible if it's root directory
		 * is not the root directory of the filesystem.
		 */
		if (mnt->mnt.mnt_root != mnt->mnt.mnt_sb->s_root)
			continue;

		/* A local view of the mount flags */
		mnt_flags = mnt->mnt.mnt_flags;

		/* Don't miss readonly hidden in the superblock flags */
		if (sb_rdonly(mnt->mnt.mnt_sb))
			mnt_flags |= MNT_LOCK_READONLY;

		/* Verify the mount flags are equal to or more permissive
		 * than the proposed new mount.
		 */
		if ((mnt_flags & MNT_LOCK_READONLY) &&
		    !(new_flags & MNT_READONLY))
			continue;
		if ((mnt_flags & MNT_LOCK_ATIME) &&
		    ((mnt_flags & MNT_ATIME_MASK) != (new_flags & MNT_ATIME_MASK)))
			continue;

		/* This mount is not fully visible if there are any
		 * locked child mounts that cover anything except for
		 * empty directories.
		 */
		list_for_each_entry(child, &mnt->mnt_mounts, mnt_child) {
			struct inode *inode = child->mnt_mountpoint->d_inode;
			/* Only worry about locked mounts */
			if (!(child->mnt.mnt_flags & MNT_LOCKED))
				continue;
			/* Is the directory permanently empty? */
			if (!is_empty_dir_inode(inode))
				goto next;
		}
		/* Preserve the locked attributes */
		*new_mnt_flags |= mnt_flags & (MNT_LOCK_READONLY | \
					       MNT_LOCK_ATIME);
		visible = true;
		goto found;
	next:	;
	}
found:
	up_read(&namespace_sem);
	return visible;
}

static bool mount_too_revealing(const struct super_block *sb, int *new_mnt_flags)
{
	const unsigned long required_iflags = SB_I_NOEXEC | SB_I_NODEV;
	struct mnt_namespace *ns = current->nsproxy->mnt_ns;
	unsigned long s_iflags;

	if (ns->user_ns == &init_user_ns)
		return false;

	/* Can this filesystem be too revealing? */
	s_iflags = sb->s_iflags;
	if (!(s_iflags & SB_I_USERNS_VISIBLE))
		return false;

	if ((s_iflags & required_iflags) != required_iflags) {
		WARN_ONCE(1, "Expected s_iflags to contain 0x%lx\n",
			  required_iflags);
		return true;
	}

	return !mnt_already_visible(ns, sb, new_mnt_flags);
}

bool mnt_may_suid(struct vfsmount *mnt)
{
	/*
	 * Foreign mounts (accessed via fchdir or through /proc
	 * symlinks) are always treated as if they are nosuid.  This
	 * prevents namespaces from trusting potentially unsafe
	 * suid/sgid bits, file caps, or security labels that originate
	 * in other namespaces.
	 */
	return !(mnt->mnt_flags & MNT_NOSUID) && check_mnt(real_mount(mnt)) &&
	       current_in_userns(mnt->mnt_sb->s_user_ns);
}

static struct ns_common *mntns_get(struct task_struct *task)
{
	struct ns_common *ns = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy) {
		ns = &nsproxy->mnt_ns->ns;
		get_mnt_ns(to_mnt_ns(ns));
	}
	task_unlock(task);

	return ns;
}

static void mntns_put(struct ns_common *ns)
{
	put_mnt_ns(to_mnt_ns(ns));
}

static int mntns_install(struct nsset *nsset, struct ns_common *ns)
{
	struct nsproxy *nsproxy = nsset->nsproxy;
	struct fs_struct *fs = nsset->fs;
	struct mnt_namespace *mnt_ns = to_mnt_ns(ns), *old_mnt_ns;
	struct user_namespace *user_ns = nsset->cred->user_ns;
	struct path root;
	int err;

	if (!ns_capable(mnt_ns->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(user_ns, CAP_SYS_CHROOT) ||
	    !ns_capable(user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	if (is_anon_ns(mnt_ns))
		return -EINVAL;

	if (fs->users != 1)
		return -EINVAL;

	get_mnt_ns(mnt_ns);
	old_mnt_ns = nsproxy->mnt_ns;
	nsproxy->mnt_ns = mnt_ns;

	/* Find the root */
	err = vfs_path_lookup(mnt_ns->root->mnt.mnt_root, &mnt_ns->root->mnt,
				"/", LOOKUP_DOWN, &root);
	if (err) {
		/* revert to old namespace */
		nsproxy->mnt_ns = old_mnt_ns;
		put_mnt_ns(mnt_ns);
		return err;
	}

	put_mnt_ns(old_mnt_ns);

	/* Update the pwd and root */
	set_fs_pwd(fs, &root);
	set_fs_root(fs, &root);

	path_put(&root);
	return 0;
}

static struct user_namespace *mntns_owner(struct ns_common *ns)
{
	return to_mnt_ns(ns)->user_ns;
}

const struct proc_ns_operations mntns_operations = {
	.name		= "mnt",
	.type		= CLONE_NEWNS,
	.get		= mntns_get,
	.put		= mntns_put,
	.install	= mntns_install,
	.owner		= mntns_owner,
};

#ifdef CONFIG_SYSCTL
static const struct ctl_table fs_namespace_sysctls[] = {
	{
		.procname	= "mount-max",
		.data		= &sysctl_mount_max,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
	},
};

static int __init init_fs_namespace_sysctls(void)
{
	register_sysctl_init("fs", fs_namespace_sysctls);
	return 0;
}
fs_initcall(init_fs_namespace_sysctls);

#endif /* CONFIG_SYSCTL */
