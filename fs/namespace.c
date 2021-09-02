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

#include "pnode.h"
#include "internal.h"

/* Maximum number of mounts in a mount namespace */
unsigned int sysctl_mount_max __read_mostly = 100000;

static unsigned int m_hash_mask __read_mostly;
static unsigned int m_hash_shift __read_mostly;
static unsigned int mp_hash_mask __read_mostly;
static unsigned int mp_hash_shift __read_mostly;

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
static DEFINE_IDA(mnt_id_ida);
static DEFINE_IDA(mnt_group_ida);

static struct hlist_head *mount_hashtable __read_mostly;
static struct hlist_head *mountpoint_hashtable __read_mostly;
static struct kmem_cache *mnt_cache __read_mostly;
static DECLARE_RWSEM(namespace_sem);
static HLIST_HEAD(unmounted);	/* protected by namespace_sem */
static LIST_HEAD(ex_mountpoints); /* protected by namespace_sem */

struct mount_kattr {
	unsigned int attr_set;
	unsigned int attr_clr;
	unsigned int propagation;
	unsigned int lookup_flags;
	bool recurse;
	struct user_namespace *mnt_userns;
};

/* /sys/fs */
struct kobject *fs_kobj;
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
	int res = ida_alloc(&mnt_id_ida, GFP_KERNEL);

	if (res < 0)
		return res;
	mnt->mnt_id = res;
	return 0;
}

static void mnt_free_id(struct mount *mnt)
{
	ida_free(&mnt_id_ida, mnt->mnt_id);
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

		if (name) {
			mnt->mnt_devname = kstrdup_const(name,
							 GFP_KERNEL_ACCOUNT);
			if (!mnt->mnt_devname)
				goto out_free_id;
		}

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
		INIT_LIST_HEAD(&mnt->mnt_slave_list);
		INIT_LIST_HEAD(&mnt->mnt_slave);
		INIT_HLIST_NODE(&mnt->mnt_mp_list);
		INIT_LIST_HEAD(&mnt->mnt_umounting);
		INIT_HLIST_HEAD(&mnt->mnt_stuck_children);
		mnt->mnt.mnt_userns = &init_user_ns;
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
	if (mnt->mnt_sb->s_readonly_remount)
		return 1;
	/* Order wrt setting s_flags/s_readonly_remount in do_remount() */
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
 * __mnt_want_write - get write access to a mount without freeze protection
 * @m: the mount on which to take a write
 *
 * This tells the low-level filesystem that a write is about to be performed to
 * it, and makes sure that writes are allowed (mnt it read-write) before
 * returning success. This operation does not protect against filesystem being
 * frozen. When the write operation is finished, __mnt_drop_write() must be
 * called. This is effectively a refcount.
 */
int __mnt_want_write(struct vfsmount *m)
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
	while (READ_ONCE(mnt->mnt.mnt_flags) & MNT_WRITE_HOLD)
		cpu_relax();
	/*
	 * After the slowpath clears MNT_WRITE_HOLD, mnt_is_readonly will
	 * be set to match its requirements. So we must not load that until
	 * MNT_WRITE_HOLD is cleared.
	 */
	smp_rmb();
	if (mnt_is_readonly(m)) {
		mnt_dec_writers(mnt);
		ret = -EROFS;
	}
	preempt_enable();

	return ret;
}

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
	ret = __mnt_want_write(m);
	if (ret)
		sb_end_write(m->mnt_sb);
	return ret;
}
EXPORT_SYMBOL_GPL(mnt_want_write);

/**
 * __mnt_want_write_file - get write access to a file's mount
 * @file: the file who's mount on which to take a write
 *
 * This is like __mnt_want_write, but if the file is already open for writing it
 * skips incrementing mnt_writers (since the open file already has a reference)
 * and instead only does the check for emergency r/o remounts.  This must be
 * paired with __mnt_drop_write_file.
 */
int __mnt_want_write_file(struct file *file)
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
	return __mnt_want_write(file->f_path.mnt);
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
	ret = __mnt_want_write_file(file);
	if (ret)
		sb_end_write(file_inode(file)->i_sb);
	return ret;
}
EXPORT_SYMBOL_GPL(mnt_want_write_file);

/**
 * __mnt_drop_write - give up write access to a mount
 * @mnt: the mount on which to give up write access
 *
 * Tells the low-level filesystem that we are done
 * performing writes to it.  Must be matched with
 * __mnt_want_write() call above.
 */
void __mnt_drop_write(struct vfsmount *mnt)
{
	preempt_disable();
	mnt_dec_writers(real_mount(mnt));
	preempt_enable();
}

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
	__mnt_drop_write(mnt);
	sb_end_write(mnt->mnt_sb);
}
EXPORT_SYMBOL_GPL(mnt_drop_write);

void __mnt_drop_write_file(struct file *file)
{
	if (!(file->f_mode & FMODE_WRITER))
		__mnt_drop_write(file->f_path.mnt);
}

void mnt_drop_write_file(struct file *file)
{
	__mnt_drop_write_file(file);
	sb_end_write(file_inode(file)->i_sb);
}
EXPORT_SYMBOL(mnt_drop_write_file);

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
			mnt->mnt.mnt_flags |= MNT_WRITE_HOLD;
			smp_mb();
			if (mnt_get_writers(mnt) > 0) {
				err = -EBUSY;
				break;
			}
		}
	}
	if (!err && atomic_long_read(&sb->s_remove_count))
		err = -EBUSY;

	if (!err) {
		sb->s_readonly_remount = 1;
		smp_wmb();
	}
	list_for_each_entry(mnt, &sb->s_mounts, mnt_instance) {
		if (mnt->mnt.mnt_flags & MNT_WRITE_HOLD)
			mnt->mnt.mnt_flags &= ~MNT_WRITE_HOLD;
	}
	unlock_mount_hash();

	return err;
}

static void free_vfsmnt(struct mount *mnt)
{
	struct user_namespace *mnt_userns;

	mnt_userns = mnt_user_ns(&mnt->mnt);
	if (mnt_userns != &init_user_ns)
		put_user_ns(mnt_userns);
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
	smp_mb();			// see mntput_no_expire()
	if (likely(!read_seqretry(&mount_lock, seq)))
		return 0;
	if (bastard->mnt_flags & MNT_SYNC_UMOUNT) {
		mnt_add_count(mnt, -1);
		return 1;
	}
	lock_mount_hash();
	if (unlikely(bastard->mnt_flags & MNT_DOOMED)) {
		mnt_add_count(mnt, -1);
		unlock_mount_hash();
		return 1;
	}
	unlock_mount_hash();
	/* caller will mntput() */
	return -1;
}

/* call under rcu_read_lock */
bool legitimize_mnt(struct vfsmount *bastard, unsigned seq)
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

/*
 * find the first mount at @dentry on vfsmount @mnt.
 * call under rcu_read_lock()
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

static inline void lock_ns_list(struct mnt_namespace *ns)
{
	spin_lock(&ns->ns_lock);
}

static inline void unlock_ns_list(struct mnt_namespace *ns)
{
	spin_unlock(&ns->ns_lock);
}

static inline bool mnt_is_cursor(struct mount *mnt)
{
	return mnt->mnt.mnt_flags & MNT_CURSOR;
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
bool __is_local_mountpoint(struct dentry *dentry)
{
	struct mnt_namespace *ns = current->nsproxy->mnt_ns;
	struct mount *mnt;
	bool is_covered = false;

	down_read(&namespace_sem);
	lock_ns_list(ns);
	list_for_each_entry(mnt, &ns->list, mnt_list) {
		if (mnt_is_cursor(mnt))
			continue;
		is_covered = (mnt->mnt_mountpoint == dentry);
		if (is_covered)
			break;
	}
	unlock_ns_list(ns);
	up_read(&namespace_sem);

	return is_covered;
}

static struct mountpoint *lookup_mountpoint(struct dentry *dentry)
{
	struct hlist_head *chain = mp_hash(dentry);
	struct mountpoint *mp;

	hlist_for_each_entry(mp, chain, m_hash) {
		if (mp->m_dentry == dentry) {
			mp->m_count++;
			return mp;
		}
	}
	return NULL;
}

static struct mountpoint *get_mountpoint(struct dentry *dentry)
{
	struct mountpoint *mp, *new = NULL;
	int ret;

	if (d_mountpoint(dentry)) {
		/* might be worth a WARN_ON() */
		if (d_unlinked(dentry))
			return ERR_PTR(-ENOENT);
mountpoint:
		read_seqlock_excl(&mount_lock);
		mp = lookup_mountpoint(dentry);
		read_sequnlock_excl(&mount_lock);
		if (mp)
			goto done;
	}

	if (!new)
		new = kmalloc(sizeof(struct mountpoint), GFP_KERNEL);
	if (!new)
		return ERR_PTR(-ENOMEM);


	/* Exactly one processes may set d_mounted */
	ret = d_set_mounted(dentry);

	/* Someone else set d_mounted? */
	if (ret == -EBUSY)
		goto mountpoint;

	/* The dentry is not available as a mountpoint? */
	mp = ERR_PTR(ret);
	if (ret)
		goto done;

	/* Add the new mountpoint to the hash table */
	read_seqlock_excl(&mount_lock);
	new->m_dentry = dget(dentry);
	new->m_count = 1;
	hlist_add_head(&new->m_hash, mp_hash(dentry));
	INIT_HLIST_HEAD(&new->m_list);
	read_sequnlock_excl(&mount_lock);

	mp = new;
	new = NULL;
done:
	kfree(new);
	return mp;
}

/*
 * vfsmount lock must be held.  Additionally, the caller is responsible
 * for serializing calls for given disposal list.
 */
static void __put_mountpoint(struct mountpoint *mp, struct list_head *list)
{
	if (!--mp->m_count) {
		struct dentry *dentry = mp->m_dentry;
		BUG_ON(!hlist_empty(&mp->m_list));
		spin_lock(&dentry->d_lock);
		dentry->d_flags &= ~DCACHE_MOUNTED;
		spin_unlock(&dentry->d_lock);
		dput_to_list(dentry, list);
		hlist_del(&mp->m_hash);
		kfree(mp);
	}
}

/* called with namespace_lock and vfsmount lock */
static void put_mountpoint(struct mountpoint *mp)
{
	__put_mountpoint(mp, &ex_mountpoints);
}

static inline int check_mnt(struct mount *mnt)
{
	return mnt->mnt_ns == current->nsproxy->mnt_ns;
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
 * vfsmount lock must be held for write
 */
static struct mountpoint *unhash_mnt(struct mount *mnt)
{
	struct mountpoint *mp;
	mnt->mnt_parent = mnt;
	mnt->mnt_mountpoint = mnt->mnt.mnt_root;
	list_del_init(&mnt->mnt_child);
	hlist_del_init_rcu(&mnt->mnt_hash);
	hlist_del_init(&mnt->mnt_mp_list);
	mp = mnt->mnt_mp;
	mnt->mnt_mp = NULL;
	return mp;
}

/*
 * vfsmount lock must be held for write
 */
static void umount_mnt(struct mount *mnt)
{
	put_mountpoint(unhash_mnt(mnt));
}

/*
 * vfsmount lock must be held for write
 */
void mnt_set_mountpoint(struct mount *mnt,
			struct mountpoint *mp,
			struct mount *child_mnt)
{
	mp->m_count++;
	mnt_add_count(mnt, 1);	/* essentially, that's mntget */
	child_mnt->mnt_mountpoint = mp->m_dentry;
	child_mnt->mnt_parent = mnt;
	child_mnt->mnt_mp = mp;
	hlist_add_head(&child_mnt->mnt_mp_list, &mp->m_list);
}

static void __attach_mnt(struct mount *mnt, struct mount *parent)
{
	hlist_add_head_rcu(&mnt->mnt_hash,
			   m_hash(&parent->mnt, mnt->mnt_mountpoint));
	list_add_tail(&mnt->mnt_child, &parent->mnt_mounts);
}

/*
 * vfsmount lock must be held for write
 */
static void attach_mnt(struct mount *mnt,
			struct mount *parent,
			struct mountpoint *mp)
{
	mnt_set_mountpoint(parent, mp, mnt);
	__attach_mnt(mnt, parent);
}

void mnt_change_mountpoint(struct mount *parent, struct mountpoint *mp, struct mount *mnt)
{
	struct mountpoint *old_mp = mnt->mnt_mp;
	struct mount *old_parent = mnt->mnt_parent;

	list_del_init(&mnt->mnt_child);
	hlist_del_init(&mnt->mnt_mp_list);
	hlist_del_init_rcu(&mnt->mnt_hash);

	attach_mnt(mnt, parent, mp);

	put_mountpoint(old_mp);
	mnt_add_count(old_parent, -1);
}

/*
 * vfsmount lock must be held for write
 */
static void commit_tree(struct mount *mnt)
{
	struct mount *parent = mnt->mnt_parent;
	struct mount *m;
	LIST_HEAD(head);
	struct mnt_namespace *n = parent->mnt_ns;

	BUG_ON(parent == mnt);

	list_add_tail(&head, &mnt->mnt_list);
	list_for_each_entry(m, &head, mnt_list)
		m->mnt_ns = n;

	list_splice(&head, n->list.prev);

	n->mounts += n->pending_mounts;
	n->pending_mounts = 0;

	__attach_mnt(mnt, parent);
	touch_mnt_namespace(n);
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

	mnt = alloc_vfsmnt(fc->source ?: "none");
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

struct vfsmount *
vfs_submount(const struct dentry *mountpoint, struct file_system_type *type,
	     const char *name, void *data)
{
	/* Until it is worked out how to pass the user namespace
	 * through from the parent mount to the submount don't support
	 * unprivileged mounts with submounts.
	 */
	if (mountpoint->d_sb->s_user_ns != &init_user_ns)
		return ERR_PTR(-EPERM);

	return vfs_kern_mount(type, SB_SUBMOUNT, name, data);
}
EXPORT_SYMBOL_GPL(vfs_submount);

static struct mount *clone_mnt(struct mount *old, struct dentry *root,
					int flag)
{
	struct super_block *sb = old->mnt.mnt_sb;
	struct mount *mnt;
	int err;

	mnt = alloc_vfsmnt(old->mnt_devname);
	if (!mnt)
		return ERR_PTR(-ENOMEM);

	if (flag & (CL_SLAVE | CL_PRIVATE | CL_SHARED_TO_SLAVE))
		mnt->mnt_group_id = 0; /* not a peer of original */
	else
		mnt->mnt_group_id = old->mnt_group_id;

	if ((flag & CL_MAKE_SHARED) && !mnt->mnt_group_id) {
		err = mnt_alloc_group_id(mnt);
		if (err)
			goto out_free;
	}

	mnt->mnt.mnt_flags = old->mnt.mnt_flags;
	mnt->mnt.mnt_flags &= ~(MNT_WRITE_HOLD|MNT_MARKED|MNT_INTERNAL);

	atomic_inc(&sb->s_active);
	mnt->mnt.mnt_userns = mnt_user_ns(&old->mnt);
	if (mnt->mnt.mnt_userns != &init_user_ns)
		mnt->mnt.mnt_userns = get_user_ns(mnt->mnt.mnt_userns);
	mnt->mnt.mnt_sb = sb;
	mnt->mnt.mnt_root = dget(root);
	mnt->mnt_mountpoint = mnt->mnt.mnt_root;
	mnt->mnt_parent = mnt;
	lock_mount_hash();
	list_add_tail(&mnt->mnt_instance, &sb->s_mounts);
	unlock_mount_hash();

	if ((flag & CL_SLAVE) ||
	    ((flag & CL_SHARED_TO_SLAVE) && IS_MNT_SHARED(old))) {
		list_add(&mnt->mnt_slave, &old->mnt_slave_list);
		mnt->mnt_master = old;
		CLEAR_MNT_SHARED(mnt);
	} else if (!(flag & CL_PRIVATE)) {
		if ((flag & CL_MAKE_SHARED) || IS_MNT_SHARED(old))
			list_add(&mnt->mnt_share, &old->mnt_share);
		if (IS_MNT_SLAVE(old))
			list_add(&mnt->mnt_slave, &old->mnt_slave);
		mnt->mnt_master = old->mnt_master;
	} else {
		CLEAR_MNT_SHARED(mnt);
	}
	if (flag & CL_MAKE_SHARED)
		set_mnt_shared(mnt);

	/* stick the duplicate mount on the same expiry list
	 * as the original if that was on one */
	if (flag & CL_EXPIRE) {
		if (!list_empty(&old->mnt_expire))
			list_add(&mnt->mnt_expire, &old->mnt_expire);
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

	if (unlikely(!list_empty(&mnt->mnt_mounts))) {
		struct mount *p, *tmp;
		list_for_each_entry_safe(p, tmp, &mnt->mnt_mounts,  mnt_child) {
			__put_mountpoint(unhash_mnt(p), &list);
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
		/* avoid cacheline pingpong, hope gcc doesn't get "smart" */
		if (unlikely(m->mnt_expiry_mark))
			m->mnt_expiry_mark = 0;
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

#ifdef CONFIG_PROC_FS
static struct mount *mnt_list_next(struct mnt_namespace *ns,
				   struct list_head *p)
{
	struct mount *mnt, *ret = NULL;

	lock_ns_list(ns);
	list_for_each_continue(p, &ns->list) {
		mnt = list_entry(p, typeof(*mnt), mnt_list);
		if (!mnt_is_cursor(mnt)) {
			ret = mnt;
			break;
		}
	}
	unlock_ns_list(ns);

	return ret;
}

/* iterator; we want it to have access to namespace_sem, thus here... */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct proc_mounts *p = m->private;
	struct list_head *prev;

	down_read(&namespace_sem);
	if (!*pos) {
		prev = &p->ns->list;
	} else {
		prev = &p->cursor.mnt_list;

		/* Read after we'd reached the end? */
		if (list_empty(prev))
			return NULL;
	}

	return mnt_list_next(p->ns, prev);
}

static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct proc_mounts *p = m->private;
	struct mount *mnt = v;

	++*pos;
	return mnt_list_next(p->ns, &mnt->mnt_list);
}

static void m_stop(struct seq_file *m, void *v)
{
	struct proc_mounts *p = m->private;
	struct mount *mnt = v;

	lock_ns_list(p->ns);
	if (mnt)
		list_move_tail(&p->cursor.mnt_list, &mnt->mnt_list);
	else
		list_del_init(&p->cursor.mnt_list);
	unlock_ns_list(p->ns);
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

void mnt_cursor_del(struct mnt_namespace *ns, struct mount *cursor)
{
	down_read(&namespace_sem);
	lock_ns_list(ns);
	list_del(&cursor->mnt_list);
	unlock_ns_list(ns);
	up_read(&namespace_sem);
}
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
	int actual_refs = 0;
	int minimum_refs = 0;
	struct mount *p;
	BUG_ON(!m);

	/* write lock needed for mnt_get_count */
	lock_mount_hash();
	for (p = mnt; p; p = next_mnt(p, mnt)) {
		actual_refs += mnt_get_count(p);
		minimum_refs += 2;
	}
	unlock_mount_hash();

	if (actual_refs > minimum_refs)
		return 0;

	return 1;
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

static void namespace_unlock(void)
{
	struct hlist_head head;
	struct hlist_node *p;
	struct mount *m;
	LIST_HEAD(list);

	hlist_move_list(&unmounted, &head);
	list_splice_init(&ex_mountpoints, &list);

	up_write(&namespace_sem);

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
		list_move(&p->mnt_list, &tmp_list);
	}

	/* Hide the mounts from mnt_mounts */
	list_for_each_entry(p, &tmp_list, mnt_list) {
		list_del_init(&p->mnt_child);
	}

	/* Add propogated mounts to the tmp_list */
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
			ns->mounts--;
			__touch_mnt_namespace(ns);
		}
		p->mnt_ns = NULL;
		if (how & UMOUNT_SYNC)
			p->mnt.mnt_flags |= MNT_SYNC_UMOUNT;

		disconnect = disconnect_mount(p, how);
		if (mnt_has_parent(p)) {
			mnt_add_count(p->mnt_parent, -1);
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
		if (mnt_get_count(mnt) != 2) {
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

	/* Recheck MNT_LOCKED with the locks held */
	retval = -EINVAL;
	if (mnt->mnt.mnt_flags & MNT_LOCKED)
		goto out;

	event++;
	if (flags & MNT_DETACH) {
		if (!list_empty(&mnt->mnt_list))
			umount_tree(mnt, UMOUNT_PROPAGATE);
		retval = 0;
	} else {
		shrink_submounts(mnt);
		retval = -EBUSY;
		if (!propagate_mount_busy(mnt, 2)) {
			if (!list_empty(&mnt->mnt_list))
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
 * The caller may hold dentry->d_inode->i_mutex.
 */
void __detach_mounts(struct dentry *dentry)
{
	struct mountpoint *mp;
	struct mount *mnt;

	namespace_lock();
	lock_mount_hash();
	mp = lookup_mountpoint(dentry);
	if (!mp)
		goto out_unlock;

	event++;
	while (!hlist_empty(&mp->m_list)) {
		mnt = hlist_entry(mp->m_list.first, struct mount, mnt_mp_list);
		if (mnt->mnt.mnt_flags & MNT_UMOUNT) {
			umount_mnt(mnt);
			hlist_add_head(&mnt->mnt_umount, &unmounted);
		}
		else umount_tree(mnt, UMOUNT_CONNECTED);
	}
	put_mountpoint(mp);
out_unlock:
	unlock_mount_hash();
	namespace_unlock();
}

/*
 * Is the caller allowed to modify his namespace?
 */
static inline bool may_mount(void)
{
	return ns_capable(current->nsproxy->mnt_ns->user_ns, CAP_SYS_ADMIN);
}

#ifdef	CONFIG_MANDATORY_FILE_LOCKING
static bool may_mandlock(void)
{
	pr_warn_once("======================================================\n"
		     "WARNING: the mand mount option is being deprecated and\n"
		     "         will be removed in v5.15!\n"
		     "======================================================\n");
	return capable(CAP_SYS_ADMIN);
}
#else
static inline bool may_mandlock(void)
{
	pr_warn("VFS: \"mand\" mount option not supported");
	return false;
}
#endif

static int can_umount(const struct path *path, int flags)
{
	struct mount *mnt = real_mount(path->mnt);

	if (!may_mount())
		return -EPERM;
	if (path->dentry != path->mnt->mnt_root)
		return -EINVAL;
	if (!check_mnt(mnt))
		return -EINVAL;
	if (mnt->mnt.mnt_flags & MNT_LOCKED) /* Check optimistically */
		return -EINVAL;
	if (flags & MNT_FORCE && !capable(CAP_SYS_ADMIN))
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
	/* Is this a proxy for a mount namespace? */
	return dentry->d_op == &ns_dentry_operations &&
	       dentry->d_fsdata == &mntns_operations;
}

static struct mnt_namespace *to_mnt_ns(struct ns_common *ns)
{
	return container_of(ns, struct mnt_namespace, ns);
}

struct ns_common *from_mnt_ns(struct mnt_namespace *mnt)
{
	return &mnt->ns;
}

static bool mnt_ns_loop(struct dentry *dentry)
{
	/* Could bind mounting the mount namespace inode cause a
	 * mount namespace loop?
	 */
	struct mnt_namespace *mnt_ns;
	if (!is_mnt_ns_file(dentry))
		return false;

	mnt_ns = to_mnt_ns(get_proc_ns(dentry->d_inode));
	return current->nsproxy->mnt_ns->seq >= mnt_ns->seq;
}

struct mount *copy_tree(struct mount *mnt, struct dentry *dentry,
					int flag)
{
	struct mount *res, *p, *q, *r, *parent;

	if (!(flag & CL_COPY_UNBINDABLE) && IS_MNT_UNBINDABLE(mnt))
		return ERR_PTR(-EINVAL);

	if (!(flag & CL_COPY_MNT_NS_FILE) && is_mnt_ns_file(dentry))
		return ERR_PTR(-EINVAL);

	res = q = clone_mnt(mnt, dentry, flag);
	if (IS_ERR(q))
		return q;

	q->mnt_mountpoint = mnt->mnt_mountpoint;

	p = mnt;
	list_for_each_entry(r, &mnt->mnt_mounts, mnt_child) {
		struct mount *s;
		if (!is_subdir(r->mnt_mountpoint, dentry))
			continue;

		for (s = r; s; s = next_mnt(s, r)) {
			if (!(flag & CL_COPY_UNBINDABLE) &&
			    IS_MNT_UNBINDABLE(s)) {
				if (s->mnt.mnt_flags & MNT_LOCKED) {
					/* Both unbindable and locked. */
					q = ERR_PTR(-EPERM);
					goto out;
				} else {
					s = skip_mnt_tree(s);
					continue;
				}
			}
			if (!(flag & CL_COPY_MNT_NS_FILE) &&
			    is_mnt_ns_file(s->mnt.mnt_root)) {
				s = skip_mnt_tree(s);
				continue;
			}
			while (p != s->mnt_parent) {
				p = p->mnt_parent;
				q = q->mnt_parent;
			}
			p = s;
			parent = q;
			q = clone_mnt(p, p->mnt.mnt_root, flag);
			if (IS_ERR(q))
				goto out;
			lock_mount_hash();
			list_add_tail(&q->mnt_list, &res->mnt_list);
			attach_mnt(q, parent, p->mnt_mp);
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
	return q;
}

/* Caller should check returned pointer for errors */

struct vfsmount *collect_mounts(const struct path *path)
{
	struct mount *tree;
	namespace_lock();
	if (!check_mnt(real_mount(path->mnt)))
		tree = ERR_PTR(-EINVAL);
	else
		tree = copy_tree(real_mount(path->mnt), path->dentry,
				 CL_COPY_ALL | CL_PRIVATE);
	namespace_unlock();
	if (IS_ERR(tree))
		return ERR_CAST(tree);
	return &tree->mnt;
}

static void free_mnt_ns(struct mnt_namespace *);
static struct mnt_namespace *alloc_mnt_ns(struct user_namespace *, bool);

void dissolve_on_fput(struct vfsmount *mnt)
{
	struct mnt_namespace *ns;
	namespace_lock();
	lock_mount_hash();
	ns = real_mount(mnt)->mnt_ns;
	if (ns) {
		if (is_anon_ns(ns))
			umount_tree(real_mount(mnt), UMOUNT_CONNECTED);
		else
			ns = NULL;
	}
	unlock_mount_hash();
	namespace_unlock();
	if (ns)
		free_mnt_ns(ns);
}

void drop_collected_mounts(struct vfsmount *mnt)
{
	namespace_lock();
	lock_mount_hash();
	umount_tree(real_mount(mnt), 0);
	unlock_mount_hash();
	namespace_unlock();
}

static bool has_locked_children(struct mount *mnt, struct dentry *dentry)
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

/**
 * clone_private_mount - create a private clone of a path
 * @path: path to clone
 *
 * This creates a new vfsmount, which will be the clone of @path.  The new mount
 * will not be attached anywhere in the namespace and will be private (i.e.
 * changes to the originating mount won't be propagated into this).
 *
 * Release with mntput().
 */
struct vfsmount *clone_private_mount(const struct path *path)
{
	struct mount *old_mnt = real_mount(path->mnt);
	struct mount *new_mnt;

	down_read(&namespace_sem);
	if (IS_MNT_UNBINDABLE(old_mnt))
		goto invalid;

	if (!check_mnt(old_mnt))
		goto invalid;

	if (has_locked_children(old_mnt, path->dentry))
		goto invalid;

	new_mnt = clone_mnt(old_mnt, path->dentry, CL_PRIVATE);
	up_read(&namespace_sem);

	if (IS_ERR(new_mnt))
		return ERR_CAST(new_mnt);

	/* Longterm mount to be removed by kern_unmount*() */
	new_mnt->mnt_ns = MNT_NS_INTERNAL;

	return &new_mnt->mnt;

invalid:
	up_read(&namespace_sem);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(clone_private_mount);

int iterate_mounts(int (*f)(struct vfsmount *, void *), void *arg,
		   struct vfsmount *root)
{
	struct mount *mnt;
	int res = f(root, arg);
	if (res)
		return res;
	list_for_each_entry(mnt, &real_mount(root)->mnt_list, mnt_list) {
		res = f(&mnt->mnt, arg);
		if (res)
			return res;
	}
	return 0;
}

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
		if (list_empty(&p->mnt_expire))
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
		if (!p->mnt_group_id && !IS_MNT_SHARED(p)) {
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
	unsigned int mounts = 0, old, pending, sum;
	struct mount *p;

	for (p = mnt; p; p = next_mnt(p, mnt))
		mounts++;

	old = ns->mounts;
	pending = ns->pending_mounts;
	sum = old + pending;
	if ((old > sum) ||
	    (pending > sum) ||
	    (max < sum) ||
	    (mounts > (max - sum)))
		return -ENOSPC;

	ns->pending_mounts = pending + mounts;
	return 0;
}

/*
 *  @source_mnt : mount tree to be attached
 *  @nd         : place the mount tree @source_mnt is attached
 *  @parent_nd  : if non-null, detach the source_mnt from its parent and
 *  		   store the parent mount and mountpoint dentry.
 *  		   (done when source_mnt is moved)
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
 */
static int attach_recursive_mnt(struct mount *source_mnt,
			struct mount *dest_mnt,
			struct mountpoint *dest_mp,
			bool moving)
{
	struct user_namespace *user_ns = current->nsproxy->mnt_ns->user_ns;
	HLIST_HEAD(tree_list);
	struct mnt_namespace *ns = dest_mnt->mnt_ns;
	struct mountpoint *smp;
	struct mount *child, *p;
	struct hlist_node *n;
	int err;

	/* Preallocate a mountpoint in case the new mounts need
	 * to be tucked under other mounts.
	 */
	smp = get_mountpoint(source_mnt->mnt.mnt_root);
	if (IS_ERR(smp))
		return PTR_ERR(smp);

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
		lock_mount_hash();
		if (err)
			goto out_cleanup_ids;
		for (p = source_mnt; p; p = next_mnt(p, source_mnt))
			set_mnt_shared(p);
	} else {
		lock_mount_hash();
	}
	if (moving) {
		unhash_mnt(source_mnt);
		attach_mnt(source_mnt, dest_mnt, dest_mp);
		touch_mnt_namespace(source_mnt->mnt_ns);
	} else {
		if (source_mnt->mnt_ns) {
			/* move from anon - the caller will destroy */
			list_del_init(&source_mnt->mnt_ns->list);
		}
		mnt_set_mountpoint(dest_mnt, dest_mp, source_mnt);
		commit_tree(source_mnt);
	}

	hlist_for_each_entry_safe(child, n, &tree_list, mnt_hash) {
		struct mount *q;
		hlist_del_init(&child->mnt_hash);
		q = __lookup_mnt(&child->mnt_parent->mnt,
				 child->mnt_mountpoint);
		if (q)
			mnt_change_mountpoint(child, smp, q);
		/* Notice when we are propagating across user namespaces */
		if (child->mnt_parent->mnt_ns->user_ns != user_ns)
			lock_mnt_tree(child);
		child->mnt.mnt_flags &= ~MNT_LOCKED;
		commit_tree(child);
	}
	put_mountpoint(smp);
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
	put_mountpoint(smp);
	read_sequnlock_excl(&mount_lock);

	return err;
}

static struct mountpoint *lock_mount(struct path *path)
{
	struct vfsmount *mnt;
	struct dentry *dentry = path->dentry;
retry:
	inode_lock(dentry->d_inode);
	if (unlikely(cant_mount(dentry))) {
		inode_unlock(dentry->d_inode);
		return ERR_PTR(-ENOENT);
	}
	namespace_lock();
	mnt = lookup_mnt(path);
	if (likely(!mnt)) {
		struct mountpoint *mp = get_mountpoint(dentry);
		if (IS_ERR(mp)) {
			namespace_unlock();
			inode_unlock(dentry->d_inode);
			return mp;
		}
		return mp;
	}
	namespace_unlock();
	inode_unlock(path->dentry->d_inode);
	path_put(path);
	path->mnt = mnt;
	dentry = path->dentry = dget(mnt->mnt_root);
	goto retry;
}

static void unlock_mount(struct mountpoint *where)
{
	struct dentry *dentry = where->m_dentry;

	read_seqlock_excl(&mount_lock);
	put_mountpoint(where);
	read_sequnlock_excl(&mount_lock);

	namespace_unlock();
	inode_unlock(dentry->d_inode);
}

static int graft_tree(struct mount *mnt, struct mount *p, struct mountpoint *mp)
{
	if (mnt->mnt.mnt_sb->s_flags & SB_NOUSER)
		return -EINVAL;

	if (d_is_dir(mp->m_dentry) !=
	      d_is_dir(mnt->mnt.mnt_root))
		return -ENOTDIR;

	return attach_recursive_mnt(mnt, p, mp, false);
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

	if (path->dentry != path->mnt->mnt_root)
		return -EINVAL;

	type = flags_to_propagation_type(ms_flags);
	if (!type)
		return -EINVAL;

	namespace_lock();
	if (type == MS_SHARED) {
		err = invent_group_ids(mnt, recurse);
		if (err)
			goto out_unlock;
	}

	lock_mount_hash();
	for (m = mnt; m; m = (recurse ? next_mnt(m, mnt) : NULL))
		change_mnt_propagation(m, type);
	unlock_mount_hash();

 out_unlock:
	namespace_unlock();
	return err;
}

static struct mount *__do_loopback(struct path *old_path, int recurse)
{
	struct mount *mnt = ERR_PTR(-EINVAL), *old = real_mount(old_path->mnt);

	if (IS_MNT_UNBINDABLE(old))
		return mnt;

	if (!check_mnt(old) && old_path->dentry->d_op != &ns_dentry_operations)
		return mnt;

	if (!recurse && has_locked_children(old, old_path->dentry))
		return mnt;

	if (recurse)
		mnt = copy_tree(old, old_path->dentry, CL_COPY_MNT_NS_FILE);
	else
		mnt = clone_mnt(old, old_path->dentry, 0);

	if (!IS_ERR(mnt))
		mnt->mnt.mnt_flags &= ~MNT_LOCKED;

	return mnt;
}

/*
 * do loopback mount.
 */
static int do_loopback(struct path *path, const char *old_name,
				int recurse)
{
	struct path old_path;
	struct mount *mnt = NULL, *parent;
	struct mountpoint *mp;
	int err;
	if (!old_name || !*old_name)
		return -EINVAL;
	err = kern_path(old_name, LOOKUP_FOLLOW|LOOKUP_AUTOMOUNT, &old_path);
	if (err)
		return err;

	err = -EINVAL;
	if (mnt_ns_loop(old_path.dentry))
		goto out;

	mp = lock_mount(path);
	if (IS_ERR(mp)) {
		err = PTR_ERR(mp);
		goto out;
	}

	parent = real_mount(path->mnt);
	if (!check_mnt(parent))
		goto out2;

	mnt = __do_loopback(&old_path, recurse);
	if (IS_ERR(mnt)) {
		err = PTR_ERR(mnt);
		goto out2;
	}

	err = graft_tree(mnt, parent, mp);
	if (err) {
		lock_mount_hash();
		umount_tree(mnt, UMOUNT_SYNC);
		unlock_mount_hash();
	}
out2:
	unlock_mount(mp);
out:
	path_put(&old_path);
	return err;
}

static struct file *open_detached_copy(struct path *path, bool recursive)
{
	struct user_namespace *user_ns = current->nsproxy->mnt_ns->user_ns;
	struct mnt_namespace *ns = alloc_mnt_ns(user_ns, true);
	struct mount *mnt, *p;
	struct file *file;

	if (IS_ERR(ns))
		return ERR_CAST(ns);

	namespace_lock();
	mnt = __do_loopback(path, recursive);
	if (IS_ERR(mnt)) {
		namespace_unlock();
		free_mnt_ns(ns);
		return ERR_CAST(mnt);
	}

	lock_mount_hash();
	for (p = mnt; p; p = next_mnt(p, mnt)) {
		p->mnt_ns = ns;
		ns->mounts++;
	}
	ns->root = mnt;
	list_add_tail(&ns->list, &mnt->mnt_list);
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

SYSCALL_DEFINE3(open_tree, int, dfd, const char __user *, filename, unsigned, flags)
{
	struct file *file;
	struct path path;
	int lookup_flags = LOOKUP_AUTOMOUNT | LOOKUP_FOLLOW;
	bool detached = flags & OPEN_TREE_CLONE;
	int error;
	int fd;

	BUILD_BUG_ON(OPEN_TREE_CLOEXEC != O_CLOEXEC);

	if (flags & ~(AT_EMPTY_PATH | AT_NO_AUTOMOUNT | AT_RECURSIVE |
		      AT_SYMLINK_NOFOLLOW | OPEN_TREE_CLONE |
		      OPEN_TREE_CLOEXEC))
		return -EINVAL;

	if ((flags & (AT_RECURSIVE | OPEN_TREE_CLONE)) == AT_RECURSIVE)
		return -EINVAL;

	if (flags & AT_NO_AUTOMOUNT)
		lookup_flags &= ~LOOKUP_AUTOMOUNT;
	if (flags & AT_SYMLINK_NOFOLLOW)
		lookup_flags &= ~LOOKUP_FOLLOW;
	if (flags & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;

	if (detached && !may_mount())
		return -EPERM;

	fd = get_unused_fd_flags(flags & O_CLOEXEC);
	if (fd < 0)
		return fd;

	error = user_path_at(dfd, filename, lookup_flags, &path);
	if (unlikely(error)) {
		file = ERR_PTR(error);
	} else {
		if (detached)
			file = open_detached_copy(&path, flags & AT_RECURSIVE);
		else
			file = dentry_open(&path, O_PATH, current_cred());
		path_put(&path);
	}
	if (IS_ERR(file)) {
		put_unused_fd(fd);
		return PTR_ERR(file);
	}
	fd_install(fd, file);
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
	   (ktime_get_real_seconds() + TIME_UPTIME_SEC_MAX > sb->s_time_max)) {
		char *buf = (char *)__get_free_page(GFP_KERNEL);
		char *mntpath = buf ? d_path(mountpoint, buf, PAGE_SIZE) : ERR_PTR(-ENOMEM);
		struct tm tm;

		time64_to_tm(sb->s_time_max, 0, &tm);

		pr_warn("%s filesystem being %s at %s supports timestamps until %04ld (0x%llx)\n",
			sb->s_type->name,
			is_mounted(mnt) ? "remounted" : "mounted",
			mntpath,
			tm.tm_year+1900, (unsigned long long)sb->s_time_max);

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

	if (path->dentry != mnt->mnt.mnt_root)
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

	if (path->dentry != path->mnt->mnt_root)
		return -EINVAL;

	if (!can_change_locked_flags(mnt, mnt_flags))
		return -EPERM;

	fc = fs_context_for_reconfigure(path->dentry, sb_flags, MS_RMT_MASK);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

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

static int do_move_mount(struct path *old_path, struct path *new_path)
{
	struct mnt_namespace *ns;
	struct mount *p;
	struct mount *old;
	struct mount *parent;
	struct mountpoint *mp, *old_mp;
	int err;
	bool attached;

	mp = lock_mount(new_path);
	if (IS_ERR(mp))
		return PTR_ERR(mp);

	old = real_mount(old_path->mnt);
	p = real_mount(new_path->mnt);
	parent = old->mnt_parent;
	attached = mnt_has_parent(old);
	old_mp = old->mnt_mp;
	ns = old->mnt_ns;

	err = -EINVAL;
	/* The mountpoint must be in our namespace. */
	if (!check_mnt(p))
		goto out;

	/* The thing moved must be mounted... */
	if (!is_mounted(&old->mnt))
		goto out;

	/* ... and either ours or the root of anon namespace */
	if (!(attached ? check_mnt(old) : is_anon_ns(ns)))
		goto out;

	if (old->mnt.mnt_flags & MNT_LOCKED)
		goto out;

	if (old_path->dentry != old_path->mnt->mnt_root)
		goto out;

	if (d_is_dir(new_path->dentry) !=
	    d_is_dir(old_path->dentry))
		goto out;
	/*
	 * Don't move a mount residing in a shared parent.
	 */
	if (attached && IS_MNT_SHARED(parent))
		goto out;
	/*
	 * Don't move a mount tree containing unbindable mounts to a destination
	 * mount which is shared.
	 */
	if (IS_MNT_SHARED(p) && tree_contains_unbindable(old))
		goto out;
	err = -ELOOP;
	if (!check_for_nsfs_mounts(old))
		goto out;
	for (; mnt_has_parent(p); p = p->mnt_parent)
		if (p == old)
			goto out;

	err = attach_recursive_mnt(old, real_mount(new_path->mnt), mp,
				   attached);
	if (err)
		goto out;

	/* if the mount is moved, it should no longer be expire
	 * automatically */
	list_del_init(&old->mnt_expire);
	if (attached)
		put_mountpoint(old_mp);
out:
	unlock_mount(mp);
	if (!err) {
		if (attached)
			mntput_no_expire(parent);
		else
			free_mnt_ns(ns);
	}
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

	err = do_move_mount(&old_path, path);
	path_put(&old_path);
	return err;
}

/*
 * add a mount into a namespace's mount tree
 */
static int do_add_mount(struct mount *newmnt, struct mountpoint *mp,
			struct path *path, int mnt_flags)
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
	if (path->mnt->mnt_sb == newmnt->mnt.mnt_sb &&
	    path->mnt->mnt_root == path->dentry)
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
	struct mountpoint *mp;
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

	mp = lock_mount(mountpoint);
	if (IS_ERR(mp)) {
		mntput(mnt);
		return PTR_ERR(mp);
	}
	error = do_add_mount(real_mount(mnt), mp, mountpoint, mnt_flags);
	unlock_mount(mp);
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

int finish_automount(struct vfsmount *m, struct path *path)
{
	struct dentry *dentry = path->dentry;
	struct mountpoint *mp;
	struct mount *mnt;
	int err;

	if (!m)
		return 0;
	if (IS_ERR(m))
		return PTR_ERR(m);

	mnt = real_mount(m);
	/* The new mount record should have at least 2 refs to prevent it being
	 * expired before we get a chance to add it
	 */
	BUG_ON(mnt_get_count(mnt) < 2);

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
	rcu_read_lock();
	if (unlikely(__lookup_mnt(path->mnt, dentry))) {
		rcu_read_unlock();
		err = 0;
		goto discard_locked;
	}
	rcu_read_unlock();
	mp = get_mountpoint(dentry);
	if (IS_ERR(mp)) {
		err = PTR_ERR(mp);
		goto discard_locked;
	}

	err = do_add_mount(mnt, mp, path, path->mnt->mnt_flags | MNT_SHRINKABLE);
	unlock_mount(mp);
	if (unlikely(err))
		goto discard;
	mntput(m);
	return 0;

discard_locked:
	namespace_unlock();
	inode_unlock(dentry->d_inode);
discard:
	/* remove m from any expiration list it may be on */
	if (!list_empty(&mnt->mnt_expire)) {
		namespace_lock();
		list_del_init(&mnt->mnt_expire);
		namespace_unlock();
	}
	mntput(m);
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
	namespace_lock();

	list_add_tail(&real_mount(mnt)->mnt_expire, expiry_list);

	namespace_unlock();
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
	 * - only referenced by its parent vfsmount
	 * - still marked for expiry (marked on the last call here; marks are
	 *   cleared by mntput())
	 */
	list_for_each_entry_safe(mnt, next, mounts, mnt_expire) {
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
	if ((flags & SB_MANDLOCK) && !may_mandlock())
		return -EPERM;

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

long do_mount(const char *dev_name, const char __user *dir_name,
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
	put_user_ns(ns->user_ns);
	kfree(ns);
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

	new_ns = kzalloc(sizeof(struct mnt_namespace), GFP_KERNEL);
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
		new_ns->seq = atomic64_add_return(1, &mnt_ns_seq);
	refcount_set(&new_ns->ns.count, 1);
	INIT_LIST_HEAD(&new_ns->list);
	init_waitqueue_head(&new_ns->poll);
	spin_lock_init(&new_ns->ns_lock);
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
		copy_flags |= CL_SHARED_TO_SLAVE;
	new = copy_tree(old, old->mnt.mnt_root, copy_flags);
	if (IS_ERR(new)) {
		namespace_unlock();
		free_mnt_ns(new_ns);
		return ERR_CAST(new);
	}
	if (user_ns != ns->user_ns) {
		lock_mount_hash();
		lock_mnt_tree(new);
		unlock_mount_hash();
	}
	new_ns->root = new;
	list_add_tail(&new_ns->list, &new->mnt_list);

	/*
	 * Second pass: switch the tsk->fs->* elements and mark new vfsmounts
	 * as belonging to new namespace.  We have already acquired a private
	 * fs_struct, so tsk->fs->lock is not needed.
	 */
	p = old;
	q = new;
	while (p) {
		q->mnt_ns = new_ns;
		new_ns->mounts++;
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
		while (p->mnt.mnt_root != q->mnt.mnt_root)
			p = next_mnt(p, old);
	}
	namespace_unlock();

	if (rootmnt)
		mntput(rootmnt);
	if (pwdmnt)
		mntput(pwdmnt);

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
	mnt->mnt_ns = ns;
	ns->root = mnt;
	ns->mounts++;
	list_add(&mnt->mnt_list, &ns->list);

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
	struct fd f;
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

	f = fdget(fs_fd);
	if (!f.file)
		return -EBADF;

	ret = -EINVAL;
	if (f.file->f_op != &fscontext_fops)
		goto err_fsfd;

	fc = f.file->private_data;

	ret = mutex_lock_interruptible(&fc->uapi_mutex);
	if (ret < 0)
		goto err_fsfd;

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

	ret = -EPERM;
	if ((fc->sb_flags & SB_MANDLOCK) && !may_mandlock())
		goto err_unlock;

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
	mnt->mnt_ns = ns;
	ns->root = mnt;
	ns->mounts = 1;
	list_add(&mnt->mnt_list, &ns->list);
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
err_fsfd:
	fdput(f);
	return ret;
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
	struct path from_path, to_path;
	unsigned int lflags;
	int ret = 0;

	if (!may_mount())
		return -EPERM;

	if (flags & ~MOVE_MOUNT__MASK)
		return -EINVAL;

	/* If someone gives a pathname, they aren't permitted to move
	 * from an fd that requires unmount as we can't get at the flag
	 * to clear it afterwards.
	 */
	lflags = 0;
	if (flags & MOVE_MOUNT_F_SYMLINKS)	lflags |= LOOKUP_FOLLOW;
	if (flags & MOVE_MOUNT_F_AUTOMOUNTS)	lflags |= LOOKUP_AUTOMOUNT;
	if (flags & MOVE_MOUNT_F_EMPTY_PATH)	lflags |= LOOKUP_EMPTY;

	ret = user_path_at(from_dfd, from_pathname, lflags, &from_path);
	if (ret < 0)
		return ret;

	lflags = 0;
	if (flags & MOVE_MOUNT_T_SYMLINKS)	lflags |= LOOKUP_FOLLOW;
	if (flags & MOVE_MOUNT_T_AUTOMOUNTS)	lflags |= LOOKUP_AUTOMOUNT;
	if (flags & MOVE_MOUNT_T_EMPTY_PATH)	lflags |= LOOKUP_EMPTY;

	ret = user_path_at(to_dfd, to_pathname, lflags, &to_path);
	if (ret < 0)
		goto out_from;

	ret = security_move_mount(&from_path, &to_path);
	if (ret < 0)
		goto out_to;

	ret = do_move_mount(&from_path, &to_path);

out_to:
	path_put(&to_path);
out_from:
	path_put(&from_path);
	return ret;
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
	struct mountpoint *old_mp, *root_mp;
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
	old_mp = lock_mount(&old);
	error = PTR_ERR(old_mp);
	if (IS_ERR(old_mp))
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
	if (root.mnt->mnt_root != root.dentry)
		goto out4; /* not a mountpoint */
	if (!mnt_has_parent(root_mnt))
		goto out4; /* not attached */
	if (new.mnt->mnt_root != new.dentry)
		goto out4; /* not a mountpoint */
	if (!mnt_has_parent(new_mnt))
		goto out4; /* not attached */
	/* make sure we can reach put_old from new_root */
	if (!is_path_reachable(old_mnt, old.dentry, &new))
		goto out4;
	/* make certain new is below the root */
	if (!is_path_reachable(new_mnt, new.dentry, &root))
		goto out4;
	lock_mount_hash();
	umount_mnt(new_mnt);
	root_mp = unhash_mnt(root_mnt);  /* we'll need its mountpoint */
	if (root_mnt->mnt.mnt_flags & MNT_LOCKED) {
		new_mnt->mnt.mnt_flags |= MNT_LOCKED;
		root_mnt->mnt.mnt_flags &= ~MNT_LOCKED;
	}
	/* mount old root on put_old */
	attach_mnt(root_mnt, old_mnt, old_mp);
	/* mount new_root on / */
	attach_mnt(new_mnt, root_parent, root_mp);
	mnt_add_count(root_parent, -1);
	touch_mnt_namespace(current->nsproxy->mnt_ns);
	/* A moved mount should not expire automatically */
	list_del_init(&new_mnt->mnt_expire);
	put_mountpoint(root_mp);
	unlock_mount_hash();
	chroot_fs_refs(&root, &new);
	error = 0;
out4:
	unlock_mount(old_mp);
	if (!error)
		mntput_no_expire(ex_parent);
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

	if (!kattr->mnt_userns)
		return 0;

	/*
	 * Once a mount has been idmapped we don't allow it to change its
	 * mapping. It makes things simpler and callers can just create
	 * another bind-mount they can idmap if they want to.
	 */
	if (mnt_user_ns(m) != &init_user_ns)
		return -EPERM;

	/* The underlying filesystem doesn't support idmapped mounts yet. */
	if (!(m->mnt_sb->s_type->fs_flags & FS_ALLOW_IDMAP))
		return -EINVAL;

	/* Don't yet support filesystem mountable in user namespaces. */
	if (m->mnt_sb->s_user_ns != &init_user_ns)
		return -EINVAL;

	/* We're not controlling the superblock. */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* Mount has already been visible in the filesystem hierarchy. */
	if (!is_anon_ns(mnt->mnt_ns))
		return -EINVAL;

	return 0;
}

static struct mount *mount_setattr_prepare(struct mount_kattr *kattr,
					   struct mount *mnt, int *err)
{
	struct mount *m = mnt, *last = NULL;

	if (!is_mounted(&m->mnt)) {
		*err = -EINVAL;
		goto out;
	}

	if (!(mnt_has_parent(m) ? check_mnt(m) : is_anon_ns(m->mnt_ns))) {
		*err = -EINVAL;
		goto out;
	}

	do {
		unsigned int flags;

		flags = recalc_flags(kattr, m);
		if (!can_change_locked_flags(m, flags)) {
			*err = -EPERM;
			goto out;
		}

		*err = can_idmap_mount(kattr, m);
		if (*err)
			goto out;

		last = m;

		if ((kattr->attr_set & MNT_READONLY) &&
		    !(m->mnt.mnt_flags & MNT_READONLY)) {
			*err = mnt_hold_writers(m);
			if (*err)
				goto out;
		}
	} while (kattr->recurse && (m = next_mnt(m, mnt)));

out:
	return last;
}

static void do_idmap_mount(const struct mount_kattr *kattr, struct mount *mnt)
{
	struct user_namespace *mnt_userns;

	if (!kattr->mnt_userns)
		return;

	mnt_userns = get_user_ns(kattr->mnt_userns);
	/* Pairs with smp_load_acquire() in mnt_user_ns(). */
	smp_store_release(&mnt->mnt.mnt_userns, mnt_userns);
}

static void mount_setattr_commit(struct mount_kattr *kattr,
				 struct mount *mnt, struct mount *last,
				 int err)
{
	struct mount *m = mnt;

	do {
		if (!err) {
			unsigned int flags;

			do_idmap_mount(kattr, m);
			flags = recalc_flags(kattr, m);
			WRITE_ONCE(m->mnt.mnt_flags, flags);
		}

		/*
		 * We either set MNT_READONLY above so make it visible
		 * before ~MNT_WRITE_HOLD or we failed to recursively
		 * apply mount options.
		 */
		if ((kattr->attr_set & MNT_READONLY) &&
		    (m->mnt.mnt_flags & MNT_WRITE_HOLD))
			mnt_unhold_writers(m);

		if (!err && kattr->propagation)
			change_mnt_propagation(m, kattr->propagation);

		/*
		 * On failure, only cleanup until we found the first mount
		 * we failed to handle.
		 */
		if (err && m == last)
			break;
	} while (kattr->recurse && (m = next_mnt(m, mnt)));

	if (!err)
		touch_mnt_namespace(mnt->mnt_ns);
}

static int do_mount_setattr(struct path *path, struct mount_kattr *kattr)
{
	struct mount *mnt = real_mount(path->mnt), *last = NULL;
	int err = 0;

	if (path->dentry != mnt->mnt.mnt_root)
		return -EINVAL;

	if (kattr->propagation) {
		/*
		 * Only take namespace_lock() if we're actually changing
		 * propagation.
		 */
		namespace_lock();
		if (kattr->propagation == MS_SHARED) {
			err = invent_group_ids(mnt, kattr->recurse);
			if (err) {
				namespace_unlock();
				return err;
			}
		}
	}

	lock_mount_hash();

	/*
	 * Get the mount tree in a shape where we can change mount
	 * properties without failure.
	 */
	last = mount_setattr_prepare(kattr, mnt, &err);
	if (last) /* Commit all changes or revert to the old state. */
		mount_setattr_commit(kattr, mnt, last, err);

	unlock_mount_hash();

	if (kattr->propagation) {
		namespace_unlock();
		if (err)
			cleanup_group_ids(mnt, NULL);
	}

	return err;
}

static int build_mount_idmapped(const struct mount_attr *attr, size_t usize,
				struct mount_kattr *kattr, unsigned int flags)
{
	int err = 0;
	struct ns_common *ns;
	struct user_namespace *mnt_userns;
	struct file *file;

	if (!((attr->attr_set | attr->attr_clr) & MOUNT_ATTR_IDMAP))
		return 0;

	/*
	 * We currently do not support clearing an idmapped mount. If this ever
	 * is a use-case we can revisit this but for now let's keep it simple
	 * and not allow it.
	 */
	if (attr->attr_clr & MOUNT_ATTR_IDMAP)
		return -EINVAL;

	if (attr->userns_fd > INT_MAX)
		return -EINVAL;

	file = fget(attr->userns_fd);
	if (!file)
		return -EBADF;

	if (!proc_ns_file(file)) {
		err = -EINVAL;
		goto out_fput;
	}

	ns = get_proc_ns(file_inode(file));
	if (ns->ops->type != CLONE_NEWUSER) {
		err = -EINVAL;
		goto out_fput;
	}

	/*
	 * The init_user_ns is used to indicate that a vfsmount is not idmapped.
	 * This is simpler than just having to treat NULL as unmapped. Users
	 * wanting to idmap a mount to init_user_ns can just use a namespace
	 * with an identity mapping.
	 */
	mnt_userns = container_of(ns, struct user_namespace, ns);
	if (mnt_userns == &init_user_ns) {
		err = -EPERM;
		goto out_fput;
	}
	kattr->mnt_userns = get_user_ns(mnt_userns);

out_fput:
	fput(file);
	return err;
}

static int build_mount_kattr(const struct mount_attr *attr, size_t usize,
			     struct mount_kattr *kattr, unsigned int flags)
{
	unsigned int lookup_flags = LOOKUP_AUTOMOUNT | LOOKUP_FOLLOW;

	if (flags & AT_NO_AUTOMOUNT)
		lookup_flags &= ~LOOKUP_AUTOMOUNT;
	if (flags & AT_SYMLINK_NOFOLLOW)
		lookup_flags &= ~LOOKUP_FOLLOW;
	if (flags & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;

	*kattr = (struct mount_kattr) {
		.lookup_flags	= lookup_flags,
		.recurse	= !!(flags & AT_RECURSIVE),
	};

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

	return build_mount_idmapped(attr, usize, kattr, flags);
}

static void finish_mount_kattr(struct mount_kattr *kattr)
{
	put_user_ns(kattr->mnt_userns);
	kattr->mnt_userns = NULL;
}

SYSCALL_DEFINE5(mount_setattr, int, dfd, const char __user *, path,
		unsigned int, flags, struct mount_attr __user *, uattr,
		size_t, usize)
{
	int err;
	struct path target;
	struct mount_attr attr;
	struct mount_kattr kattr;

	BUILD_BUG_ON(sizeof(struct mount_attr) != MOUNT_ATTR_SIZE_VER0);

	if (flags & ~(AT_EMPTY_PATH |
		      AT_RECURSIVE |
		      AT_SYMLINK_NOFOLLOW |
		      AT_NO_AUTOMOUNT))
		return -EINVAL;

	if (unlikely(usize > PAGE_SIZE))
		return -E2BIG;
	if (unlikely(usize < MOUNT_ATTR_SIZE_VER0))
		return -EINVAL;

	if (!may_mount())
		return -EPERM;

	err = copy_struct_from_user(&attr, sizeof(attr), uattr, usize);
	if (err)
		return err;

	/* Don't bother walking through the mounts if this is a nop. */
	if (attr.attr_set == 0 &&
	    attr.attr_clr == 0 &&
	    attr.propagation == 0)
		return 0;

	err = build_mount_kattr(&attr, usize, &kattr, flags);
	if (err)
		return err;

	err = user_path_at(dfd, path, kattr.lookup_flags, &target);
	if (err)
		return err;

	err = do_mount_setattr(&target, &kattr);
	finish_mount_kattr(&kattr);
	path_put(&target);
	return err;
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

	ns = alloc_mnt_ns(&init_user_ns, false);
	if (IS_ERR(ns))
		panic("Can't allocate initial namespace");
	m = real_mount(mnt);
	m->mnt_ns = ns;
	ns->root = m;
	ns->mounts = 1;
	list_add(&m->mnt_list, &ns->list);
	init_task.nsproxy->mnt_ns = ns;
	get_mnt_ns(ns);

	root.mnt = mnt;
	root.dentry = mnt->mnt_root;
	mnt->mnt_flags |= MNT_LOCKED;

	set_fs_pwd(current->fs, &root);
	set_fs_root(current->fs, &root);
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
	drop_collected_mounts(&ns->root->mnt);
	free_mnt_ns(ns);
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
	if (!IS_ERR_OR_NULL(mnt)) {
		real_mount(mnt)->mnt_ns = NULL;
		synchronize_rcu();	/* yecchhh... */
		mntput(mnt);
	}
}
EXPORT_SYMBOL(kern_unmount);

void kern_unmount_array(struct vfsmount *mnt[], unsigned int num)
{
	unsigned int i;

	for (i = 0; i < num; i++)
		if (mnt[i])
			real_mount(mnt[i])->mnt_ns = NULL;
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
	struct mount *mnt;
	bool visible = false;

	down_read(&namespace_sem);
	lock_ns_list(ns);
	list_for_each_entry(mnt, &ns->list, mnt_list) {
		struct mount *child;
		int mnt_flags;

		if (mnt_is_cursor(mnt))
			continue;

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
			/* Is the directory permanetly empty? */
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
	unlock_ns_list(ns);
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
