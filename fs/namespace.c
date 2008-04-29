/*
 *  linux/fs/namespace.c
 *
 * (C) Copyright Al Viro 2000, 2001
 *	Released under GPL v2.
 *
 * Based on code from fs/super.c, copyright Linus Torvalds and others.
 * Heavily rewritten.
 */

#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/acct.h>
#include <linux/capability.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/seq_file.h>
#include <linux/mnt_namespace.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/ramfs.h>
#include <linux/log2.h>
#include <linux/idr.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include "pnode.h"
#include "internal.h"

#define HASH_SHIFT ilog2(PAGE_SIZE / sizeof(struct list_head))
#define HASH_SIZE (1UL << HASH_SHIFT)

/* spinlock for vfsmount related operations, inplace of dcache_lock */
__cacheline_aligned_in_smp DEFINE_SPINLOCK(vfsmount_lock);

static int event;
static DEFINE_IDA(mnt_id_ida);
static DEFINE_IDA(mnt_group_ida);

static struct list_head *mount_hashtable __read_mostly;
static struct kmem_cache *mnt_cache __read_mostly;
static struct rw_semaphore namespace_sem;

/* /sys/fs */
struct kobject *fs_kobj;
EXPORT_SYMBOL_GPL(fs_kobj);

static inline unsigned long hash(struct vfsmount *mnt, struct dentry *dentry)
{
	unsigned long tmp = ((unsigned long)mnt / L1_CACHE_BYTES);
	tmp += ((unsigned long)dentry / L1_CACHE_BYTES);
	tmp = tmp + (tmp >> HASH_SHIFT);
	return tmp & (HASH_SIZE - 1);
}

#define MNT_WRITER_UNDERFLOW_LIMIT -(1<<16)

/* allocation is serialized by namespace_sem */
static int mnt_alloc_id(struct vfsmount *mnt)
{
	int res;

retry:
	ida_pre_get(&mnt_id_ida, GFP_KERNEL);
	spin_lock(&vfsmount_lock);
	res = ida_get_new(&mnt_id_ida, &mnt->mnt_id);
	spin_unlock(&vfsmount_lock);
	if (res == -EAGAIN)
		goto retry;

	return res;
}

static void mnt_free_id(struct vfsmount *mnt)
{
	spin_lock(&vfsmount_lock);
	ida_remove(&mnt_id_ida, mnt->mnt_id);
	spin_unlock(&vfsmount_lock);
}

/*
 * Allocate a new peer group ID
 *
 * mnt_group_ida is protected by namespace_sem
 */
static int mnt_alloc_group_id(struct vfsmount *mnt)
{
	if (!ida_pre_get(&mnt_group_ida, GFP_KERNEL))
		return -ENOMEM;

	return ida_get_new_above(&mnt_group_ida, 1, &mnt->mnt_group_id);
}

/*
 * Release a peer group ID
 */
void mnt_release_group_id(struct vfsmount *mnt)
{
	ida_remove(&mnt_group_ida, mnt->mnt_group_id);
	mnt->mnt_group_id = 0;
}

struct vfsmount *alloc_vfsmnt(const char *name)
{
	struct vfsmount *mnt = kmem_cache_zalloc(mnt_cache, GFP_KERNEL);
	if (mnt) {
		int err;

		err = mnt_alloc_id(mnt);
		if (err) {
			kmem_cache_free(mnt_cache, mnt);
			return NULL;
		}

		atomic_set(&mnt->mnt_count, 1);
		INIT_LIST_HEAD(&mnt->mnt_hash);
		INIT_LIST_HEAD(&mnt->mnt_child);
		INIT_LIST_HEAD(&mnt->mnt_mounts);
		INIT_LIST_HEAD(&mnt->mnt_list);
		INIT_LIST_HEAD(&mnt->mnt_expire);
		INIT_LIST_HEAD(&mnt->mnt_share);
		INIT_LIST_HEAD(&mnt->mnt_slave_list);
		INIT_LIST_HEAD(&mnt->mnt_slave);
		atomic_set(&mnt->__mnt_writers, 0);
		if (name) {
			int size = strlen(name) + 1;
			char *newname = kmalloc(size, GFP_KERNEL);
			if (newname) {
				memcpy(newname, name, size);
				mnt->mnt_devname = newname;
			}
		}
	}
	return mnt;
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
int __mnt_is_readonly(struct vfsmount *mnt)
{
	if (mnt->mnt_flags & MNT_READONLY)
		return 1;
	if (mnt->mnt_sb->s_flags & MS_RDONLY)
		return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(__mnt_is_readonly);

struct mnt_writer {
	/*
	 * If holding multiple instances of this lock, they
	 * must be ordered by cpu number.
	 */
	spinlock_t lock;
	struct lock_class_key lock_class; /* compiles out with !lockdep */
	unsigned long count;
	struct vfsmount *mnt;
} ____cacheline_aligned_in_smp;
static DEFINE_PER_CPU(struct mnt_writer, mnt_writers);

static int __init init_mnt_writers(void)
{
	int cpu;
	for_each_possible_cpu(cpu) {
		struct mnt_writer *writer = &per_cpu(mnt_writers, cpu);
		spin_lock_init(&writer->lock);
		lockdep_set_class(&writer->lock, &writer->lock_class);
		writer->count = 0;
	}
	return 0;
}
fs_initcall(init_mnt_writers);

static void unlock_mnt_writers(void)
{
	int cpu;
	struct mnt_writer *cpu_writer;

	for_each_possible_cpu(cpu) {
		cpu_writer = &per_cpu(mnt_writers, cpu);
		spin_unlock(&cpu_writer->lock);
	}
}

static inline void __clear_mnt_count(struct mnt_writer *cpu_writer)
{
	if (!cpu_writer->mnt)
		return;
	/*
	 * This is in case anyone ever leaves an invalid,
	 * old ->mnt and a count of 0.
	 */
	if (!cpu_writer->count)
		return;
	atomic_add(cpu_writer->count, &cpu_writer->mnt->__mnt_writers);
	cpu_writer->count = 0;
}
 /*
 * must hold cpu_writer->lock
 */
static inline void use_cpu_writer_for_mount(struct mnt_writer *cpu_writer,
					  struct vfsmount *mnt)
{
	if (cpu_writer->mnt == mnt)
		return;
	__clear_mnt_count(cpu_writer);
	cpu_writer->mnt = mnt;
}

/*
 * Most r/o checks on a fs are for operations that take
 * discrete amounts of time, like a write() or unlink().
 * We must keep track of when those operations start
 * (for permission checks) and when they end, so that
 * we can determine when writes are able to occur to
 * a filesystem.
 */
/**
 * mnt_want_write - get write access to a mount
 * @mnt: the mount on which to take a write
 *
 * This tells the low-level filesystem that a write is
 * about to be performed to it, and makes sure that
 * writes are allowed before returning success.  When
 * the write operation is finished, mnt_drop_write()
 * must be called.  This is effectively a refcount.
 */
int mnt_want_write(struct vfsmount *mnt)
{
	int ret = 0;
	struct mnt_writer *cpu_writer;

	cpu_writer = &get_cpu_var(mnt_writers);
	spin_lock(&cpu_writer->lock);
	if (__mnt_is_readonly(mnt)) {
		ret = -EROFS;
		goto out;
	}
	use_cpu_writer_for_mount(cpu_writer, mnt);
	cpu_writer->count++;
out:
	spin_unlock(&cpu_writer->lock);
	put_cpu_var(mnt_writers);
	return ret;
}
EXPORT_SYMBOL_GPL(mnt_want_write);

static void lock_mnt_writers(void)
{
	int cpu;
	struct mnt_writer *cpu_writer;

	for_each_possible_cpu(cpu) {
		cpu_writer = &per_cpu(mnt_writers, cpu);
		spin_lock(&cpu_writer->lock);
		__clear_mnt_count(cpu_writer);
		cpu_writer->mnt = NULL;
	}
}

/*
 * These per-cpu write counts are not guaranteed to have
 * matched increments and decrements on any given cpu.
 * A file open()ed for write on one cpu and close()d on
 * another cpu will imbalance this count.  Make sure it
 * does not get too far out of whack.
 */
static void handle_write_count_underflow(struct vfsmount *mnt)
{
	if (atomic_read(&mnt->__mnt_writers) >=
	    MNT_WRITER_UNDERFLOW_LIMIT)
		return;
	/*
	 * It isn't necessary to hold all of the locks
	 * at the same time, but doing it this way makes
	 * us share a lot more code.
	 */
	lock_mnt_writers();
	/*
	 * vfsmount_lock is for mnt_flags.
	 */
	spin_lock(&vfsmount_lock);
	/*
	 * If coalescing the per-cpu writer counts did not
	 * get us back to a positive writer count, we have
	 * a bug.
	 */
	if ((atomic_read(&mnt->__mnt_writers) < 0) &&
	    !(mnt->mnt_flags & MNT_IMBALANCED_WRITE_COUNT)) {
		printk(KERN_DEBUG "leak detected on mount(%p) writers "
				"count: %d\n",
			mnt, atomic_read(&mnt->__mnt_writers));
		WARN_ON(1);
		/* use the flag to keep the dmesg spam down */
		mnt->mnt_flags |= MNT_IMBALANCED_WRITE_COUNT;
	}
	spin_unlock(&vfsmount_lock);
	unlock_mnt_writers();
}

/**
 * mnt_drop_write - give up write access to a mount
 * @mnt: the mount on which to give up write access
 *
 * Tells the low-level filesystem that we are done
 * performing writes to it.  Must be matched with
 * mnt_want_write() call above.
 */
void mnt_drop_write(struct vfsmount *mnt)
{
	int must_check_underflow = 0;
	struct mnt_writer *cpu_writer;

	cpu_writer = &get_cpu_var(mnt_writers);
	spin_lock(&cpu_writer->lock);

	use_cpu_writer_for_mount(cpu_writer, mnt);
	if (cpu_writer->count > 0) {
		cpu_writer->count--;
	} else {
		must_check_underflow = 1;
		atomic_dec(&mnt->__mnt_writers);
	}

	spin_unlock(&cpu_writer->lock);
	/*
	 * Logically, we could call this each time,
	 * but the __mnt_writers cacheline tends to
	 * be cold, and makes this expensive.
	 */
	if (must_check_underflow)
		handle_write_count_underflow(mnt);
	/*
	 * This could be done right after the spinlock
	 * is taken because the spinlock keeps us on
	 * the cpu, and disables preemption.  However,
	 * putting it here bounds the amount that
	 * __mnt_writers can underflow.  Without it,
	 * we could theoretically wrap __mnt_writers.
	 */
	put_cpu_var(mnt_writers);
}
EXPORT_SYMBOL_GPL(mnt_drop_write);

static int mnt_make_readonly(struct vfsmount *mnt)
{
	int ret = 0;

	lock_mnt_writers();
	/*
	 * With all the locks held, this value is stable
	 */
	if (atomic_read(&mnt->__mnt_writers) > 0) {
		ret = -EBUSY;
		goto out;
	}
	/*
	 * nobody can do a successful mnt_want_write() with all
	 * of the counts in MNT_DENIED_WRITE and the locks held.
	 */
	spin_lock(&vfsmount_lock);
	if (!ret)
		mnt->mnt_flags |= MNT_READONLY;
	spin_unlock(&vfsmount_lock);
out:
	unlock_mnt_writers();
	return ret;
}

static void __mnt_unmake_readonly(struct vfsmount *mnt)
{
	spin_lock(&vfsmount_lock);
	mnt->mnt_flags &= ~MNT_READONLY;
	spin_unlock(&vfsmount_lock);
}

int simple_set_mnt(struct vfsmount *mnt, struct super_block *sb)
{
	mnt->mnt_sb = sb;
	mnt->mnt_root = dget(sb->s_root);
	return 0;
}

EXPORT_SYMBOL(simple_set_mnt);

void free_vfsmnt(struct vfsmount *mnt)
{
	kfree(mnt->mnt_devname);
	mnt_free_id(mnt);
	kmem_cache_free(mnt_cache, mnt);
}

/*
 * find the first or last mount at @dentry on vfsmount @mnt depending on
 * @dir. If @dir is set return the first mount else return the last mount.
 */
struct vfsmount *__lookup_mnt(struct vfsmount *mnt, struct dentry *dentry,
			      int dir)
{
	struct list_head *head = mount_hashtable + hash(mnt, dentry);
	struct list_head *tmp = head;
	struct vfsmount *p, *found = NULL;

	for (;;) {
		tmp = dir ? tmp->next : tmp->prev;
		p = NULL;
		if (tmp == head)
			break;
		p = list_entry(tmp, struct vfsmount, mnt_hash);
		if (p->mnt_parent == mnt && p->mnt_mountpoint == dentry) {
			found = p;
			break;
		}
	}
	return found;
}

/*
 * lookup_mnt increments the ref count before returning
 * the vfsmount struct.
 */
struct vfsmount *lookup_mnt(struct vfsmount *mnt, struct dentry *dentry)
{
	struct vfsmount *child_mnt;
	spin_lock(&vfsmount_lock);
	if ((child_mnt = __lookup_mnt(mnt, dentry, 1)))
		mntget(child_mnt);
	spin_unlock(&vfsmount_lock);
	return child_mnt;
}

static inline int check_mnt(struct vfsmount *mnt)
{
	return mnt->mnt_ns == current->nsproxy->mnt_ns;
}

static void touch_mnt_namespace(struct mnt_namespace *ns)
{
	if (ns) {
		ns->event = ++event;
		wake_up_interruptible(&ns->poll);
	}
}

static void __touch_mnt_namespace(struct mnt_namespace *ns)
{
	if (ns && ns->event != event) {
		ns->event = event;
		wake_up_interruptible(&ns->poll);
	}
}

static void detach_mnt(struct vfsmount *mnt, struct path *old_path)
{
	old_path->dentry = mnt->mnt_mountpoint;
	old_path->mnt = mnt->mnt_parent;
	mnt->mnt_parent = mnt;
	mnt->mnt_mountpoint = mnt->mnt_root;
	list_del_init(&mnt->mnt_child);
	list_del_init(&mnt->mnt_hash);
	old_path->dentry->d_mounted--;
}

void mnt_set_mountpoint(struct vfsmount *mnt, struct dentry *dentry,
			struct vfsmount *child_mnt)
{
	child_mnt->mnt_parent = mntget(mnt);
	child_mnt->mnt_mountpoint = dget(dentry);
	dentry->d_mounted++;
}

static void attach_mnt(struct vfsmount *mnt, struct path *path)
{
	mnt_set_mountpoint(path->mnt, path->dentry, mnt);
	list_add_tail(&mnt->mnt_hash, mount_hashtable +
			hash(path->mnt, path->dentry));
	list_add_tail(&mnt->mnt_child, &path->mnt->mnt_mounts);
}

/*
 * the caller must hold vfsmount_lock
 */
static void commit_tree(struct vfsmount *mnt)
{
	struct vfsmount *parent = mnt->mnt_parent;
	struct vfsmount *m;
	LIST_HEAD(head);
	struct mnt_namespace *n = parent->mnt_ns;

	BUG_ON(parent == mnt);

	list_add_tail(&head, &mnt->mnt_list);
	list_for_each_entry(m, &head, mnt_list)
		m->mnt_ns = n;
	list_splice(&head, n->list.prev);

	list_add_tail(&mnt->mnt_hash, mount_hashtable +
				hash(parent, mnt->mnt_mountpoint));
	list_add_tail(&mnt->mnt_child, &parent->mnt_mounts);
	touch_mnt_namespace(n);
}

static struct vfsmount *next_mnt(struct vfsmount *p, struct vfsmount *root)
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
	return list_entry(next, struct vfsmount, mnt_child);
}

static struct vfsmount *skip_mnt_tree(struct vfsmount *p)
{
	struct list_head *prev = p->mnt_mounts.prev;
	while (prev != &p->mnt_mounts) {
		p = list_entry(prev, struct vfsmount, mnt_child);
		prev = p->mnt_mounts.prev;
	}
	return p;
}

static struct vfsmount *clone_mnt(struct vfsmount *old, struct dentry *root,
					int flag)
{
	struct super_block *sb = old->mnt_sb;
	struct vfsmount *mnt = alloc_vfsmnt(old->mnt_devname);

	if (mnt) {
		if (flag & (CL_SLAVE | CL_PRIVATE))
			mnt->mnt_group_id = 0; /* not a peer of original */
		else
			mnt->mnt_group_id = old->mnt_group_id;

		if ((flag & CL_MAKE_SHARED) && !mnt->mnt_group_id) {
			int err = mnt_alloc_group_id(mnt);
			if (err)
				goto out_free;
		}

		mnt->mnt_flags = old->mnt_flags;
		atomic_inc(&sb->s_active);
		mnt->mnt_sb = sb;
		mnt->mnt_root = dget(root);
		mnt->mnt_mountpoint = mnt->mnt_root;
		mnt->mnt_parent = mnt;

		if (flag & CL_SLAVE) {
			list_add(&mnt->mnt_slave, &old->mnt_slave_list);
			mnt->mnt_master = old;
			CLEAR_MNT_SHARED(mnt);
		} else if (!(flag & CL_PRIVATE)) {
			if ((flag & CL_PROPAGATION) || IS_MNT_SHARED(old))
				list_add(&mnt->mnt_share, &old->mnt_share);
			if (IS_MNT_SLAVE(old))
				list_add(&mnt->mnt_slave, &old->mnt_slave);
			mnt->mnt_master = old->mnt_master;
		}
		if (flag & CL_MAKE_SHARED)
			set_mnt_shared(mnt);

		/* stick the duplicate mount on the same expiry list
		 * as the original if that was on one */
		if (flag & CL_EXPIRE) {
			if (!list_empty(&old->mnt_expire))
				list_add(&mnt->mnt_expire, &old->mnt_expire);
		}
	}
	return mnt;

 out_free:
	free_vfsmnt(mnt);
	return NULL;
}

static inline void __mntput(struct vfsmount *mnt)
{
	int cpu;
	struct super_block *sb = mnt->mnt_sb;
	/*
	 * We don't have to hold all of the locks at the
	 * same time here because we know that we're the
	 * last reference to mnt and that no new writers
	 * can come in.
	 */
	for_each_possible_cpu(cpu) {
		struct mnt_writer *cpu_writer = &per_cpu(mnt_writers, cpu);
		if (cpu_writer->mnt != mnt)
			continue;
		spin_lock(&cpu_writer->lock);
		atomic_add(cpu_writer->count, &mnt->__mnt_writers);
		cpu_writer->count = 0;
		/*
		 * Might as well do this so that no one
		 * ever sees the pointer and expects
		 * it to be valid.
		 */
		cpu_writer->mnt = NULL;
		spin_unlock(&cpu_writer->lock);
	}
	/*
	 * This probably indicates that somebody messed
	 * up a mnt_want/drop_write() pair.  If this
	 * happens, the filesystem was probably unable
	 * to make r/w->r/o transitions.
	 */
	WARN_ON(atomic_read(&mnt->__mnt_writers));
	dput(mnt->mnt_root);
	free_vfsmnt(mnt);
	deactivate_super(sb);
}

void mntput_no_expire(struct vfsmount *mnt)
{
repeat:
	if (atomic_dec_and_lock(&mnt->mnt_count, &vfsmount_lock)) {
		if (likely(!mnt->mnt_pinned)) {
			spin_unlock(&vfsmount_lock);
			__mntput(mnt);
			return;
		}
		atomic_add(mnt->mnt_pinned + 1, &mnt->mnt_count);
		mnt->mnt_pinned = 0;
		spin_unlock(&vfsmount_lock);
		acct_auto_close_mnt(mnt);
		security_sb_umount_close(mnt);
		goto repeat;
	}
}

EXPORT_SYMBOL(mntput_no_expire);

void mnt_pin(struct vfsmount *mnt)
{
	spin_lock(&vfsmount_lock);
	mnt->mnt_pinned++;
	spin_unlock(&vfsmount_lock);
}

EXPORT_SYMBOL(mnt_pin);

void mnt_unpin(struct vfsmount *mnt)
{
	spin_lock(&vfsmount_lock);
	if (mnt->mnt_pinned) {
		atomic_inc(&mnt->mnt_count);
		mnt->mnt_pinned--;
	}
	spin_unlock(&vfsmount_lock);
}

EXPORT_SYMBOL(mnt_unpin);

static inline void mangle(struct seq_file *m, const char *s)
{
	seq_escape(m, s, " \t\n\\");
}

/*
 * Simple .show_options callback for filesystems which don't want to
 * implement more complex mount option showing.
 *
 * See also save_mount_options().
 */
int generic_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	const char *options = mnt->mnt_sb->s_options;

	if (options != NULL && options[0]) {
		seq_putc(m, ',');
		mangle(m, options);
	}

	return 0;
}
EXPORT_SYMBOL(generic_show_options);

/*
 * If filesystem uses generic_show_options(), this function should be
 * called from the fill_super() callback.
 *
 * The .remount_fs callback usually needs to be handled in a special
 * way, to make sure, that previous options are not overwritten if the
 * remount fails.
 *
 * Also note, that if the filesystem's .remount_fs function doesn't
 * reset all options to their default value, but changes only newly
 * given options, then the displayed options will not reflect reality
 * any more.
 */
void save_mount_options(struct super_block *sb, char *options)
{
	kfree(sb->s_options);
	sb->s_options = kstrdup(options, GFP_KERNEL);
}
EXPORT_SYMBOL(save_mount_options);

#ifdef CONFIG_PROC_FS
/* iterator */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct proc_mounts *p = m->private;

	down_read(&namespace_sem);
	return seq_list_start(&p->ns->list, *pos);
}

static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct proc_mounts *p = m->private;

	return seq_list_next(v, &p->ns->list, pos);
}

static void m_stop(struct seq_file *m, void *v)
{
	up_read(&namespace_sem);
}

struct proc_fs_info {
	int flag;
	const char *str;
};

static void show_sb_opts(struct seq_file *m, struct super_block *sb)
{
	static const struct proc_fs_info fs_info[] = {
		{ MS_SYNCHRONOUS, ",sync" },
		{ MS_DIRSYNC, ",dirsync" },
		{ MS_MANDLOCK, ",mand" },
		{ 0, NULL }
	};
	const struct proc_fs_info *fs_infop;

	for (fs_infop = fs_info; fs_infop->flag; fs_infop++) {
		if (sb->s_flags & fs_infop->flag)
			seq_puts(m, fs_infop->str);
	}
}

static void show_mnt_opts(struct seq_file *m, struct vfsmount *mnt)
{
	static const struct proc_fs_info mnt_info[] = {
		{ MNT_NOSUID, ",nosuid" },
		{ MNT_NODEV, ",nodev" },
		{ MNT_NOEXEC, ",noexec" },
		{ MNT_NOATIME, ",noatime" },
		{ MNT_NODIRATIME, ",nodiratime" },
		{ MNT_RELATIME, ",relatime" },
		{ 0, NULL }
	};
	const struct proc_fs_info *fs_infop;

	for (fs_infop = mnt_info; fs_infop->flag; fs_infop++) {
		if (mnt->mnt_flags & fs_infop->flag)
			seq_puts(m, fs_infop->str);
	}
}

static void show_type(struct seq_file *m, struct super_block *sb)
{
	mangle(m, sb->s_type->name);
	if (sb->s_subtype && sb->s_subtype[0]) {
		seq_putc(m, '.');
		mangle(m, sb->s_subtype);
	}
}

static int show_vfsmnt(struct seq_file *m, void *v)
{
	struct vfsmount *mnt = list_entry(v, struct vfsmount, mnt_list);
	int err = 0;
	struct path mnt_path = { .dentry = mnt->mnt_root, .mnt = mnt };

	mangle(m, mnt->mnt_devname ? mnt->mnt_devname : "none");
	seq_putc(m, ' ');
	seq_path(m, &mnt_path, " \t\n\\");
	seq_putc(m, ' ');
	show_type(m, mnt->mnt_sb);
	seq_puts(m, __mnt_is_readonly(mnt) ? " ro" : " rw");
	show_sb_opts(m, mnt->mnt_sb);
	show_mnt_opts(m, mnt);
	if (mnt->mnt_sb->s_op->show_options)
		err = mnt->mnt_sb->s_op->show_options(m, mnt);
	seq_puts(m, " 0 0\n");
	return err;
}

const struct seq_operations mounts_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_vfsmnt
};

static int show_mountinfo(struct seq_file *m, void *v)
{
	struct proc_mounts *p = m->private;
	struct vfsmount *mnt = list_entry(v, struct vfsmount, mnt_list);
	struct super_block *sb = mnt->mnt_sb;
	struct path mnt_path = { .dentry = mnt->mnt_root, .mnt = mnt };
	struct path root = p->root;
	int err = 0;

	seq_printf(m, "%i %i %u:%u ", mnt->mnt_id, mnt->mnt_parent->mnt_id,
		   MAJOR(sb->s_dev), MINOR(sb->s_dev));
	seq_dentry(m, mnt->mnt_root, " \t\n\\");
	seq_putc(m, ' ');
	seq_path_root(m, &mnt_path, &root, " \t\n\\");
	if (root.mnt != p->root.mnt || root.dentry != p->root.dentry) {
		/*
		 * Mountpoint is outside root, discard that one.  Ugly,
		 * but less so than trying to do that in iterator in a
		 * race-free way (due to renames).
		 */
		return SEQ_SKIP;
	}
	seq_puts(m, mnt->mnt_flags & MNT_READONLY ? " ro" : " rw");
	show_mnt_opts(m, mnt);

	/* Tagged fields ("foo:X" or "bar") */
	if (IS_MNT_SHARED(mnt))
		seq_printf(m, " shared:%i", mnt->mnt_group_id);
	if (IS_MNT_SLAVE(mnt)) {
		int master = mnt->mnt_master->mnt_group_id;
		int dom = get_dominating_id(mnt, &p->root);
		seq_printf(m, " master:%i", master);
		if (dom && dom != master)
			seq_printf(m, " propagate_from:%i", dom);
	}
	if (IS_MNT_UNBINDABLE(mnt))
		seq_puts(m, " unbindable");

	/* Filesystem specific data */
	seq_puts(m, " - ");
	show_type(m, sb);
	seq_putc(m, ' ');
	mangle(m, mnt->mnt_devname ? mnt->mnt_devname : "none");
	seq_puts(m, sb->s_flags & MS_RDONLY ? " ro" : " rw");
	show_sb_opts(m, sb);
	if (sb->s_op->show_options)
		err = sb->s_op->show_options(m, mnt);
	seq_putc(m, '\n');
	return err;
}

const struct seq_operations mountinfo_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_mountinfo,
};

static int show_vfsstat(struct seq_file *m, void *v)
{
	struct vfsmount *mnt = list_entry(v, struct vfsmount, mnt_list);
	struct path mnt_path = { .dentry = mnt->mnt_root, .mnt = mnt };
	int err = 0;

	/* device */
	if (mnt->mnt_devname) {
		seq_puts(m, "device ");
		mangle(m, mnt->mnt_devname);
	} else
		seq_puts(m, "no device");

	/* mount point */
	seq_puts(m, " mounted on ");
	seq_path(m, &mnt_path, " \t\n\\");
	seq_putc(m, ' ');

	/* file system type */
	seq_puts(m, "with fstype ");
	show_type(m, mnt->mnt_sb);

	/* optional statistics */
	if (mnt->mnt_sb->s_op->show_stats) {
		seq_putc(m, ' ');
		err = mnt->mnt_sb->s_op->show_stats(m, mnt);
	}

	seq_putc(m, '\n');
	return err;
}

const struct seq_operations mountstats_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_vfsstat,
};
#endif  /* CONFIG_PROC_FS */

/**
 * may_umount_tree - check if a mount tree is busy
 * @mnt: root of mount tree
 *
 * This is called to check if a tree of mounts has any
 * open files, pwds, chroots or sub mounts that are
 * busy.
 */
int may_umount_tree(struct vfsmount *mnt)
{
	int actual_refs = 0;
	int minimum_refs = 0;
	struct vfsmount *p;

	spin_lock(&vfsmount_lock);
	for (p = mnt; p; p = next_mnt(p, mnt)) {
		actual_refs += atomic_read(&p->mnt_count);
		minimum_refs += 2;
	}
	spin_unlock(&vfsmount_lock);

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
	spin_lock(&vfsmount_lock);
	if (propagate_mount_busy(mnt, 2))
		ret = 0;
	spin_unlock(&vfsmount_lock);
	return ret;
}

EXPORT_SYMBOL(may_umount);

void release_mounts(struct list_head *head)
{
	struct vfsmount *mnt;
	while (!list_empty(head)) {
		mnt = list_first_entry(head, struct vfsmount, mnt_hash);
		list_del_init(&mnt->mnt_hash);
		if (mnt->mnt_parent != mnt) {
			struct dentry *dentry;
			struct vfsmount *m;
			spin_lock(&vfsmount_lock);
			dentry = mnt->mnt_mountpoint;
			m = mnt->mnt_parent;
			mnt->mnt_mountpoint = mnt->mnt_root;
			mnt->mnt_parent = mnt;
			m->mnt_ghosts--;
			spin_unlock(&vfsmount_lock);
			dput(dentry);
			mntput(m);
		}
		mntput(mnt);
	}
}

void umount_tree(struct vfsmount *mnt, int propagate, struct list_head *kill)
{
	struct vfsmount *p;

	for (p = mnt; p; p = next_mnt(p, mnt))
		list_move(&p->mnt_hash, kill);

	if (propagate)
		propagate_umount(kill);

	list_for_each_entry(p, kill, mnt_hash) {
		list_del_init(&p->mnt_expire);
		list_del_init(&p->mnt_list);
		__touch_mnt_namespace(p->mnt_ns);
		p->mnt_ns = NULL;
		list_del_init(&p->mnt_child);
		if (p->mnt_parent != p) {
			p->mnt_parent->mnt_ghosts++;
			p->mnt_mountpoint->d_mounted--;
		}
		change_mnt_propagation(p, MS_PRIVATE);
	}
}

static void shrink_submounts(struct vfsmount *mnt, struct list_head *umounts);

static int do_umount(struct vfsmount *mnt, int flags)
{
	struct super_block *sb = mnt->mnt_sb;
	int retval;
	LIST_HEAD(umount_list);

	retval = security_sb_umount(mnt, flags);
	if (retval)
		return retval;

	/*
	 * Allow userspace to request a mountpoint be expired rather than
	 * unmounting unconditionally. Unmount only happens if:
	 *  (1) the mark is already set (the mark is cleared by mntput())
	 *  (2) the usage count == 1 [parent vfsmount] + 1 [sys_umount]
	 */
	if (flags & MNT_EXPIRE) {
		if (mnt == current->fs->root.mnt ||
		    flags & (MNT_FORCE | MNT_DETACH))
			return -EINVAL;

		if (atomic_read(&mnt->mnt_count) != 2)
			return -EBUSY;

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
		lock_kernel();
		sb->s_op->umount_begin(sb);
		unlock_kernel();
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
	if (mnt == current->fs->root.mnt && !(flags & MNT_DETACH)) {
		/*
		 * Special case for "unmounting" root ...
		 * we just try to remount it readonly.
		 */
		down_write(&sb->s_umount);
		if (!(sb->s_flags & MS_RDONLY)) {
			lock_kernel();
			retval = do_remount_sb(sb, MS_RDONLY, NULL, 0);
			unlock_kernel();
		}
		up_write(&sb->s_umount);
		return retval;
	}

	down_write(&namespace_sem);
	spin_lock(&vfsmount_lock);
	event++;

	if (!(flags & MNT_DETACH))
		shrink_submounts(mnt, &umount_list);

	retval = -EBUSY;
	if (flags & MNT_DETACH || !propagate_mount_busy(mnt, 2)) {
		if (!list_empty(&mnt->mnt_list))
			umount_tree(mnt, 1, &umount_list);
		retval = 0;
	}
	spin_unlock(&vfsmount_lock);
	if (retval)
		security_sb_umount_busy(mnt);
	up_write(&namespace_sem);
	release_mounts(&umount_list);
	return retval;
}

/*
 * Now umount can handle mount points as well as block devices.
 * This is important for filesystems which use unnamed block devices.
 *
 * We now support a flag for forced unmount like the other 'big iron'
 * unixes. Our API is identical to OSF/1 to avoid making a mess of AMD
 */

asmlinkage long sys_umount(char __user * name, int flags)
{
	struct nameidata nd;
	int retval;

	retval = __user_walk(name, LOOKUP_FOLLOW, &nd);
	if (retval)
		goto out;
	retval = -EINVAL;
	if (nd.path.dentry != nd.path.mnt->mnt_root)
		goto dput_and_out;
	if (!check_mnt(nd.path.mnt))
		goto dput_and_out;

	retval = -EPERM;
	if (!capable(CAP_SYS_ADMIN))
		goto dput_and_out;

	retval = do_umount(nd.path.mnt, flags);
dput_and_out:
	/* we mustn't call path_put() as that would clear mnt_expiry_mark */
	dput(nd.path.dentry);
	mntput_no_expire(nd.path.mnt);
out:
	return retval;
}

#ifdef __ARCH_WANT_SYS_OLDUMOUNT

/*
 *	The 2.0 compatible umount. No flags.
 */
asmlinkage long sys_oldumount(char __user * name)
{
	return sys_umount(name, 0);
}

#endif

static int mount_is_safe(struct nameidata *nd)
{
	if (capable(CAP_SYS_ADMIN))
		return 0;
	return -EPERM;
#ifdef notyet
	if (S_ISLNK(nd->path.dentry->d_inode->i_mode))
		return -EPERM;
	if (nd->path.dentry->d_inode->i_mode & S_ISVTX) {
		if (current->uid != nd->path.dentry->d_inode->i_uid)
			return -EPERM;
	}
	if (vfs_permission(nd, MAY_WRITE))
		return -EPERM;
	return 0;
#endif
}

struct vfsmount *copy_tree(struct vfsmount *mnt, struct dentry *dentry,
					int flag)
{
	struct vfsmount *res, *p, *q, *r, *s;
	struct path path;

	if (!(flag & CL_COPY_ALL) && IS_MNT_UNBINDABLE(mnt))
		return NULL;

	res = q = clone_mnt(mnt, dentry, flag);
	if (!q)
		goto Enomem;
	q->mnt_mountpoint = mnt->mnt_mountpoint;

	p = mnt;
	list_for_each_entry(r, &mnt->mnt_mounts, mnt_child) {
		if (!is_subdir(r->mnt_mountpoint, dentry))
			continue;

		for (s = r; s; s = next_mnt(s, r)) {
			if (!(flag & CL_COPY_ALL) && IS_MNT_UNBINDABLE(s)) {
				s = skip_mnt_tree(s);
				continue;
			}
			while (p != s->mnt_parent) {
				p = p->mnt_parent;
				q = q->mnt_parent;
			}
			p = s;
			path.mnt = q;
			path.dentry = p->mnt_mountpoint;
			q = clone_mnt(p, p->mnt_root, flag);
			if (!q)
				goto Enomem;
			spin_lock(&vfsmount_lock);
			list_add_tail(&q->mnt_list, &res->mnt_list);
			attach_mnt(q, &path);
			spin_unlock(&vfsmount_lock);
		}
	}
	return res;
Enomem:
	if (res) {
		LIST_HEAD(umount_list);
		spin_lock(&vfsmount_lock);
		umount_tree(res, 0, &umount_list);
		spin_unlock(&vfsmount_lock);
		release_mounts(&umount_list);
	}
	return NULL;
}

struct vfsmount *collect_mounts(struct vfsmount *mnt, struct dentry *dentry)
{
	struct vfsmount *tree;
	down_write(&namespace_sem);
	tree = copy_tree(mnt, dentry, CL_COPY_ALL | CL_PRIVATE);
	up_write(&namespace_sem);
	return tree;
}

void drop_collected_mounts(struct vfsmount *mnt)
{
	LIST_HEAD(umount_list);
	down_write(&namespace_sem);
	spin_lock(&vfsmount_lock);
	umount_tree(mnt, 0, &umount_list);
	spin_unlock(&vfsmount_lock);
	up_write(&namespace_sem);
	release_mounts(&umount_list);
}

static void cleanup_group_ids(struct vfsmount *mnt, struct vfsmount *end)
{
	struct vfsmount *p;

	for (p = mnt; p != end; p = next_mnt(p, mnt)) {
		if (p->mnt_group_id && !IS_MNT_SHARED(p))
			mnt_release_group_id(p);
	}
}

static int invent_group_ids(struct vfsmount *mnt, bool recurse)
{
	struct vfsmount *p;

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
static int attach_recursive_mnt(struct vfsmount *source_mnt,
			struct path *path, struct path *parent_path)
{
	LIST_HEAD(tree_list);
	struct vfsmount *dest_mnt = path->mnt;
	struct dentry *dest_dentry = path->dentry;
	struct vfsmount *child, *p;
	int err;

	if (IS_MNT_SHARED(dest_mnt)) {
		err = invent_group_ids(source_mnt, true);
		if (err)
			goto out;
	}
	err = propagate_mnt(dest_mnt, dest_dentry, source_mnt, &tree_list);
	if (err)
		goto out_cleanup_ids;

	if (IS_MNT_SHARED(dest_mnt)) {
		for (p = source_mnt; p; p = next_mnt(p, source_mnt))
			set_mnt_shared(p);
	}

	spin_lock(&vfsmount_lock);
	if (parent_path) {
		detach_mnt(source_mnt, parent_path);
		attach_mnt(source_mnt, path);
		touch_mnt_namespace(current->nsproxy->mnt_ns);
	} else {
		mnt_set_mountpoint(dest_mnt, dest_dentry, source_mnt);
		commit_tree(source_mnt);
	}

	list_for_each_entry_safe(child, p, &tree_list, mnt_hash) {
		list_del_init(&child->mnt_hash);
		commit_tree(child);
	}
	spin_unlock(&vfsmount_lock);
	return 0;

 out_cleanup_ids:
	if (IS_MNT_SHARED(dest_mnt))
		cleanup_group_ids(source_mnt, NULL);
 out:
	return err;
}

static int graft_tree(struct vfsmount *mnt, struct path *path)
{
	int err;
	if (mnt->mnt_sb->s_flags & MS_NOUSER)
		return -EINVAL;

	if (S_ISDIR(path->dentry->d_inode->i_mode) !=
	      S_ISDIR(mnt->mnt_root->d_inode->i_mode))
		return -ENOTDIR;

	err = -ENOENT;
	mutex_lock(&path->dentry->d_inode->i_mutex);
	if (IS_DEADDIR(path->dentry->d_inode))
		goto out_unlock;

	err = security_sb_check_sb(mnt, path);
	if (err)
		goto out_unlock;

	err = -ENOENT;
	if (IS_ROOT(path->dentry) || !d_unhashed(path->dentry))
		err = attach_recursive_mnt(mnt, path, NULL);
out_unlock:
	mutex_unlock(&path->dentry->d_inode->i_mutex);
	if (!err)
		security_sb_post_addmount(mnt, path);
	return err;
}

/*
 * recursively change the type of the mountpoint.
 * noinline this do_mount helper to save do_mount stack space.
 */
static noinline int do_change_type(struct nameidata *nd, int flag)
{
	struct vfsmount *m, *mnt = nd->path.mnt;
	int recurse = flag & MS_REC;
	int type = flag & ~MS_REC;
	int err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (nd->path.dentry != nd->path.mnt->mnt_root)
		return -EINVAL;

	down_write(&namespace_sem);
	if (type == MS_SHARED) {
		err = invent_group_ids(mnt, recurse);
		if (err)
			goto out_unlock;
	}

	spin_lock(&vfsmount_lock);
	for (m = mnt; m; m = (recurse ? next_mnt(m, mnt) : NULL))
		change_mnt_propagation(m, type);
	spin_unlock(&vfsmount_lock);

 out_unlock:
	up_write(&namespace_sem);
	return err;
}

/*
 * do loopback mount.
 * noinline this do_mount helper to save do_mount stack space.
 */
static noinline int do_loopback(struct nameidata *nd, char *old_name,
				int recurse)
{
	struct nameidata old_nd;
	struct vfsmount *mnt = NULL;
	int err = mount_is_safe(nd);
	if (err)
		return err;
	if (!old_name || !*old_name)
		return -EINVAL;
	err = path_lookup(old_name, LOOKUP_FOLLOW, &old_nd);
	if (err)
		return err;

	down_write(&namespace_sem);
	err = -EINVAL;
	if (IS_MNT_UNBINDABLE(old_nd.path.mnt))
		goto out;

	if (!check_mnt(nd->path.mnt) || !check_mnt(old_nd.path.mnt))
		goto out;

	err = -ENOMEM;
	if (recurse)
		mnt = copy_tree(old_nd.path.mnt, old_nd.path.dentry, 0);
	else
		mnt = clone_mnt(old_nd.path.mnt, old_nd.path.dentry, 0);

	if (!mnt)
		goto out;

	err = graft_tree(mnt, &nd->path);
	if (err) {
		LIST_HEAD(umount_list);
		spin_lock(&vfsmount_lock);
		umount_tree(mnt, 0, &umount_list);
		spin_unlock(&vfsmount_lock);
		release_mounts(&umount_list);
	}

out:
	up_write(&namespace_sem);
	path_put(&old_nd.path);
	return err;
}

static int change_mount_flags(struct vfsmount *mnt, int ms_flags)
{
	int error = 0;
	int readonly_request = 0;

	if (ms_flags & MS_RDONLY)
		readonly_request = 1;
	if (readonly_request == __mnt_is_readonly(mnt))
		return 0;

	if (readonly_request)
		error = mnt_make_readonly(mnt);
	else
		__mnt_unmake_readonly(mnt);
	return error;
}

/*
 * change filesystem flags. dir should be a physical root of filesystem.
 * If you've mounted a non-root directory somewhere and want to do remount
 * on it - tough luck.
 * noinline this do_mount helper to save do_mount stack space.
 */
static noinline int do_remount(struct nameidata *nd, int flags, int mnt_flags,
		      void *data)
{
	int err;
	struct super_block *sb = nd->path.mnt->mnt_sb;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!check_mnt(nd->path.mnt))
		return -EINVAL;

	if (nd->path.dentry != nd->path.mnt->mnt_root)
		return -EINVAL;

	down_write(&sb->s_umount);
	if (flags & MS_BIND)
		err = change_mount_flags(nd->path.mnt, flags);
	else
		err = do_remount_sb(sb, flags, data, 0);
	if (!err)
		nd->path.mnt->mnt_flags = mnt_flags;
	up_write(&sb->s_umount);
	if (!err)
		security_sb_post_remount(nd->path.mnt, flags, data);
	return err;
}

static inline int tree_contains_unbindable(struct vfsmount *mnt)
{
	struct vfsmount *p;
	for (p = mnt; p; p = next_mnt(p, mnt)) {
		if (IS_MNT_UNBINDABLE(p))
			return 1;
	}
	return 0;
}

/*
 * noinline this do_mount helper to save do_mount stack space.
 */
static noinline int do_move_mount(struct nameidata *nd, char *old_name)
{
	struct nameidata old_nd;
	struct path parent_path;
	struct vfsmount *p;
	int err = 0;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!old_name || !*old_name)
		return -EINVAL;
	err = path_lookup(old_name, LOOKUP_FOLLOW, &old_nd);
	if (err)
		return err;

	down_write(&namespace_sem);
	while (d_mountpoint(nd->path.dentry) &&
	       follow_down(&nd->path.mnt, &nd->path.dentry))
		;
	err = -EINVAL;
	if (!check_mnt(nd->path.mnt) || !check_mnt(old_nd.path.mnt))
		goto out;

	err = -ENOENT;
	mutex_lock(&nd->path.dentry->d_inode->i_mutex);
	if (IS_DEADDIR(nd->path.dentry->d_inode))
		goto out1;

	if (!IS_ROOT(nd->path.dentry) && d_unhashed(nd->path.dentry))
		goto out1;

	err = -EINVAL;
	if (old_nd.path.dentry != old_nd.path.mnt->mnt_root)
		goto out1;

	if (old_nd.path.mnt == old_nd.path.mnt->mnt_parent)
		goto out1;

	if (S_ISDIR(nd->path.dentry->d_inode->i_mode) !=
	      S_ISDIR(old_nd.path.dentry->d_inode->i_mode))
		goto out1;
	/*
	 * Don't move a mount residing in a shared parent.
	 */
	if (old_nd.path.mnt->mnt_parent &&
	    IS_MNT_SHARED(old_nd.path.mnt->mnt_parent))
		goto out1;
	/*
	 * Don't move a mount tree containing unbindable mounts to a destination
	 * mount which is shared.
	 */
	if (IS_MNT_SHARED(nd->path.mnt) &&
	    tree_contains_unbindable(old_nd.path.mnt))
		goto out1;
	err = -ELOOP;
	for (p = nd->path.mnt; p->mnt_parent != p; p = p->mnt_parent)
		if (p == old_nd.path.mnt)
			goto out1;

	err = attach_recursive_mnt(old_nd.path.mnt, &nd->path, &parent_path);
	if (err)
		goto out1;

	/* if the mount is moved, it should no longer be expire
	 * automatically */
	list_del_init(&old_nd.path.mnt->mnt_expire);
out1:
	mutex_unlock(&nd->path.dentry->d_inode->i_mutex);
out:
	up_write(&namespace_sem);
	if (!err)
		path_put(&parent_path);
	path_put(&old_nd.path);
	return err;
}

/*
 * create a new mount for userspace and request it to be added into the
 * namespace's tree
 * noinline this do_mount helper to save do_mount stack space.
 */
static noinline int do_new_mount(struct nameidata *nd, char *type, int flags,
			int mnt_flags, char *name, void *data)
{
	struct vfsmount *mnt;

	if (!type || !memchr(type, 0, PAGE_SIZE))
		return -EINVAL;

	/* we need capabilities... */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	mnt = do_kern_mount(type, flags, name, data);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);

	return do_add_mount(mnt, nd, mnt_flags, NULL);
}

/*
 * add a mount into a namespace's mount tree
 * - provide the option of adding the new mount to an expiration list
 */
int do_add_mount(struct vfsmount *newmnt, struct nameidata *nd,
		 int mnt_flags, struct list_head *fslist)
{
	int err;

	down_write(&namespace_sem);
	/* Something was mounted here while we slept */
	while (d_mountpoint(nd->path.dentry) &&
	       follow_down(&nd->path.mnt, &nd->path.dentry))
		;
	err = -EINVAL;
	if (!check_mnt(nd->path.mnt))
		goto unlock;

	/* Refuse the same filesystem on the same mount point */
	err = -EBUSY;
	if (nd->path.mnt->mnt_sb == newmnt->mnt_sb &&
	    nd->path.mnt->mnt_root == nd->path.dentry)
		goto unlock;

	err = -EINVAL;
	if (S_ISLNK(newmnt->mnt_root->d_inode->i_mode))
		goto unlock;

	newmnt->mnt_flags = mnt_flags;
	if ((err = graft_tree(newmnt, &nd->path)))
		goto unlock;

	if (fslist) /* add to the specified expiration list */
		list_add_tail(&newmnt->mnt_expire, fslist);

	up_write(&namespace_sem);
	return 0;

unlock:
	up_write(&namespace_sem);
	mntput(newmnt);
	return err;
}

EXPORT_SYMBOL_GPL(do_add_mount);

/*
 * process a list of expirable mountpoints with the intent of discarding any
 * mountpoints that aren't in use and haven't been touched since last we came
 * here
 */
void mark_mounts_for_expiry(struct list_head *mounts)
{
	struct vfsmount *mnt, *next;
	LIST_HEAD(graveyard);
	LIST_HEAD(umounts);

	if (list_empty(mounts))
		return;

	down_write(&namespace_sem);
	spin_lock(&vfsmount_lock);

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
		mnt = list_first_entry(&graveyard, struct vfsmount, mnt_expire);
		touch_mnt_namespace(mnt->mnt_ns);
		umount_tree(mnt, 1, &umounts);
	}
	spin_unlock(&vfsmount_lock);
	up_write(&namespace_sem);

	release_mounts(&umounts);
}

EXPORT_SYMBOL_GPL(mark_mounts_for_expiry);

/*
 * Ripoff of 'select_parent()'
 *
 * search the list of submounts for a given mountpoint, and move any
 * shrinkable submounts to the 'graveyard' list.
 */
static int select_submounts(struct vfsmount *parent, struct list_head *graveyard)
{
	struct vfsmount *this_parent = parent;
	struct list_head *next;
	int found = 0;

repeat:
	next = this_parent->mnt_mounts.next;
resume:
	while (next != &this_parent->mnt_mounts) {
		struct list_head *tmp = next;
		struct vfsmount *mnt = list_entry(tmp, struct vfsmount, mnt_child);

		next = tmp->next;
		if (!(mnt->mnt_flags & MNT_SHRINKABLE))
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
 */
static void shrink_submounts(struct vfsmount *mnt, struct list_head *umounts)
{
	LIST_HEAD(graveyard);
	struct vfsmount *m;

	/* extract submounts of 'mountpoint' from the expiration list */
	while (select_submounts(mnt, &graveyard)) {
		while (!list_empty(&graveyard)) {
			m = list_first_entry(&graveyard, struct vfsmount,
						mnt_expire);
			touch_mnt_namespace(mnt->mnt_ns);
			umount_tree(mnt, 1, umounts);
		}
	}
}

/*
 * Some copy_from_user() implementations do not return the exact number of
 * bytes remaining to copy on a fault.  But copy_mount_options() requires that.
 * Note that this function differs from copy_from_user() in that it will oops
 * on bad values of `to', rather than returning a short copy.
 */
static long exact_copy_from_user(void *to, const void __user * from,
				 unsigned long n)
{
	char *t = to;
	const char __user *f = from;
	char c;

	if (!access_ok(VERIFY_READ, from, n))
		return n;

	while (n) {
		if (__get_user(c, f)) {
			memset(t, 0, n);
			break;
		}
		*t++ = c;
		f++;
		n--;
	}
	return n;
}

int copy_mount_options(const void __user * data, unsigned long *where)
{
	int i;
	unsigned long page;
	unsigned long size;

	*where = 0;
	if (!data)
		return 0;

	if (!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	/* We only care that *some* data at the address the user
	 * gave us is valid.  Just in case, we'll zero
	 * the remainder of the page.
	 */
	/* copy_from_user cannot cross TASK_SIZE ! */
	size = TASK_SIZE - (unsigned long)data;
	if (size > PAGE_SIZE)
		size = PAGE_SIZE;

	i = size - exact_copy_from_user((void *)page, data, size);
	if (!i) {
		free_page(page);
		return -EFAULT;
	}
	if (i != PAGE_SIZE)
		memset((char *)page + i, 0, PAGE_SIZE - i);
	*where = page;
	return 0;
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
long do_mount(char *dev_name, char *dir_name, char *type_page,
		  unsigned long flags, void *data_page)
{
	struct nameidata nd;
	int retval = 0;
	int mnt_flags = 0;

	/* Discard magic */
	if ((flags & MS_MGC_MSK) == MS_MGC_VAL)
		flags &= ~MS_MGC_MSK;

	/* Basic sanity checks */

	if (!dir_name || !*dir_name || !memchr(dir_name, 0, PAGE_SIZE))
		return -EINVAL;
	if (dev_name && !memchr(dev_name, 0, PAGE_SIZE))
		return -EINVAL;

	if (data_page)
		((char *)data_page)[PAGE_SIZE - 1] = 0;

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
	if (flags & MS_RELATIME)
		mnt_flags |= MNT_RELATIME;
	if (flags & MS_RDONLY)
		mnt_flags |= MNT_READONLY;

	flags &= ~(MS_NOSUID | MS_NOEXEC | MS_NODEV | MS_ACTIVE |
		   MS_NOATIME | MS_NODIRATIME | MS_RELATIME| MS_KERNMOUNT);

	/* ... and get the mountpoint */
	retval = path_lookup(dir_name, LOOKUP_FOLLOW, &nd);
	if (retval)
		return retval;

	retval = security_sb_mount(dev_name, &nd.path,
				   type_page, flags, data_page);
	if (retval)
		goto dput_out;

	if (flags & MS_REMOUNT)
		retval = do_remount(&nd, flags & ~MS_REMOUNT, mnt_flags,
				    data_page);
	else if (flags & MS_BIND)
		retval = do_loopback(&nd, dev_name, flags & MS_REC);
	else if (flags & (MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE))
		retval = do_change_type(&nd, flags);
	else if (flags & MS_MOVE)
		retval = do_move_mount(&nd, dev_name);
	else
		retval = do_new_mount(&nd, type_page, flags, mnt_flags,
				      dev_name, data_page);
dput_out:
	path_put(&nd.path);
	return retval;
}

/*
 * Allocate a new namespace structure and populate it with contents
 * copied from the namespace of the passed in task structure.
 */
static struct mnt_namespace *dup_mnt_ns(struct mnt_namespace *mnt_ns,
		struct fs_struct *fs)
{
	struct mnt_namespace *new_ns;
	struct vfsmount *rootmnt = NULL, *pwdmnt = NULL, *altrootmnt = NULL;
	struct vfsmount *p, *q;

	new_ns = kmalloc(sizeof(struct mnt_namespace), GFP_KERNEL);
	if (!new_ns)
		return ERR_PTR(-ENOMEM);

	atomic_set(&new_ns->count, 1);
	INIT_LIST_HEAD(&new_ns->list);
	init_waitqueue_head(&new_ns->poll);
	new_ns->event = 0;

	down_write(&namespace_sem);
	/* First pass: copy the tree topology */
	new_ns->root = copy_tree(mnt_ns->root, mnt_ns->root->mnt_root,
					CL_COPY_ALL | CL_EXPIRE);
	if (!new_ns->root) {
		up_write(&namespace_sem);
		kfree(new_ns);
		return ERR_PTR(-ENOMEM);;
	}
	spin_lock(&vfsmount_lock);
	list_add_tail(&new_ns->list, &new_ns->root->mnt_list);
	spin_unlock(&vfsmount_lock);

	/*
	 * Second pass: switch the tsk->fs->* elements and mark new vfsmounts
	 * as belonging to new namespace.  We have already acquired a private
	 * fs_struct, so tsk->fs->lock is not needed.
	 */
	p = mnt_ns->root;
	q = new_ns->root;
	while (p) {
		q->mnt_ns = new_ns;
		if (fs) {
			if (p == fs->root.mnt) {
				rootmnt = p;
				fs->root.mnt = mntget(q);
			}
			if (p == fs->pwd.mnt) {
				pwdmnt = p;
				fs->pwd.mnt = mntget(q);
			}
			if (p == fs->altroot.mnt) {
				altrootmnt = p;
				fs->altroot.mnt = mntget(q);
			}
		}
		p = next_mnt(p, mnt_ns->root);
		q = next_mnt(q, new_ns->root);
	}
	up_write(&namespace_sem);

	if (rootmnt)
		mntput(rootmnt);
	if (pwdmnt)
		mntput(pwdmnt);
	if (altrootmnt)
		mntput(altrootmnt);

	return new_ns;
}

struct mnt_namespace *copy_mnt_ns(unsigned long flags, struct mnt_namespace *ns,
		struct fs_struct *new_fs)
{
	struct mnt_namespace *new_ns;

	BUG_ON(!ns);
	get_mnt_ns(ns);

	if (!(flags & CLONE_NEWNS))
		return ns;

	new_ns = dup_mnt_ns(ns, new_fs);

	put_mnt_ns(ns);
	return new_ns;
}

asmlinkage long sys_mount(char __user * dev_name, char __user * dir_name,
			  char __user * type, unsigned long flags,
			  void __user * data)
{
	int retval;
	unsigned long data_page;
	unsigned long type_page;
	unsigned long dev_page;
	char *dir_page;

	retval = copy_mount_options(type, &type_page);
	if (retval < 0)
		return retval;

	dir_page = getname(dir_name);
	retval = PTR_ERR(dir_page);
	if (IS_ERR(dir_page))
		goto out1;

	retval = copy_mount_options(dev_name, &dev_page);
	if (retval < 0)
		goto out2;

	retval = copy_mount_options(data, &data_page);
	if (retval < 0)
		goto out3;

	lock_kernel();
	retval = do_mount((char *)dev_page, dir_page, (char *)type_page,
			  flags, (void *)data_page);
	unlock_kernel();
	free_page(data_page);

out3:
	free_page(dev_page);
out2:
	putname(dir_page);
out1:
	free_page(type_page);
	return retval;
}

/*
 * Replace the fs->{rootmnt,root} with {mnt,dentry}. Put the old values.
 * It can block. Requires the big lock held.
 */
void set_fs_root(struct fs_struct *fs, struct path *path)
{
	struct path old_root;

	write_lock(&fs->lock);
	old_root = fs->root;
	fs->root = *path;
	path_get(path);
	write_unlock(&fs->lock);
	if (old_root.dentry)
		path_put(&old_root);
}

/*
 * Replace the fs->{pwdmnt,pwd} with {mnt,dentry}. Put the old values.
 * It can block. Requires the big lock held.
 */
void set_fs_pwd(struct fs_struct *fs, struct path *path)
{
	struct path old_pwd;

	write_lock(&fs->lock);
	old_pwd = fs->pwd;
	fs->pwd = *path;
	path_get(path);
	write_unlock(&fs->lock);

	if (old_pwd.dentry)
		path_put(&old_pwd);
}

static void chroot_fs_refs(struct path *old_root, struct path *new_root)
{
	struct task_struct *g, *p;
	struct fs_struct *fs;

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		task_lock(p);
		fs = p->fs;
		if (fs) {
			atomic_inc(&fs->count);
			task_unlock(p);
			if (fs->root.dentry == old_root->dentry
			    && fs->root.mnt == old_root->mnt)
				set_fs_root(fs, new_root);
			if (fs->pwd.dentry == old_root->dentry
			    && fs->pwd.mnt == old_root->mnt)
				set_fs_pwd(fs, new_root);
			put_fs_struct(fs);
		} else
			task_unlock(p);
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);
}

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
 * See Documentation/filesystems/ramfs-rootfs-initramfs.txt for alternatives
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
asmlinkage long sys_pivot_root(const char __user * new_root,
			       const char __user * put_old)
{
	struct vfsmount *tmp;
	struct nameidata new_nd, old_nd;
	struct path parent_path, root_parent, root;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	error = __user_walk(new_root, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
			    &new_nd);
	if (error)
		goto out0;
	error = -EINVAL;
	if (!check_mnt(new_nd.path.mnt))
		goto out1;

	error = __user_walk(put_old, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &old_nd);
	if (error)
		goto out1;

	error = security_sb_pivotroot(&old_nd.path, &new_nd.path);
	if (error) {
		path_put(&old_nd.path);
		goto out1;
	}

	read_lock(&current->fs->lock);
	root = current->fs->root;
	path_get(&current->fs->root);
	read_unlock(&current->fs->lock);
	down_write(&namespace_sem);
	mutex_lock(&old_nd.path.dentry->d_inode->i_mutex);
	error = -EINVAL;
	if (IS_MNT_SHARED(old_nd.path.mnt) ||
		IS_MNT_SHARED(new_nd.path.mnt->mnt_parent) ||
		IS_MNT_SHARED(root.mnt->mnt_parent))
		goto out2;
	if (!check_mnt(root.mnt))
		goto out2;
	error = -ENOENT;
	if (IS_DEADDIR(new_nd.path.dentry->d_inode))
		goto out2;
	if (d_unhashed(new_nd.path.dentry) && !IS_ROOT(new_nd.path.dentry))
		goto out2;
	if (d_unhashed(old_nd.path.dentry) && !IS_ROOT(old_nd.path.dentry))
		goto out2;
	error = -EBUSY;
	if (new_nd.path.mnt == root.mnt ||
	    old_nd.path.mnt == root.mnt)
		goto out2; /* loop, on the same file system  */
	error = -EINVAL;
	if (root.mnt->mnt_root != root.dentry)
		goto out2; /* not a mountpoint */
	if (root.mnt->mnt_parent == root.mnt)
		goto out2; /* not attached */
	if (new_nd.path.mnt->mnt_root != new_nd.path.dentry)
		goto out2; /* not a mountpoint */
	if (new_nd.path.mnt->mnt_parent == new_nd.path.mnt)
		goto out2; /* not attached */
	/* make sure we can reach put_old from new_root */
	tmp = old_nd.path.mnt;
	spin_lock(&vfsmount_lock);
	if (tmp != new_nd.path.mnt) {
		for (;;) {
			if (tmp->mnt_parent == tmp)
				goto out3; /* already mounted on put_old */
			if (tmp->mnt_parent == new_nd.path.mnt)
				break;
			tmp = tmp->mnt_parent;
		}
		if (!is_subdir(tmp->mnt_mountpoint, new_nd.path.dentry))
			goto out3;
	} else if (!is_subdir(old_nd.path.dentry, new_nd.path.dentry))
		goto out3;
	detach_mnt(new_nd.path.mnt, &parent_path);
	detach_mnt(root.mnt, &root_parent);
	/* mount old root on put_old */
	attach_mnt(root.mnt, &old_nd.path);
	/* mount new_root on / */
	attach_mnt(new_nd.path.mnt, &root_parent);
	touch_mnt_namespace(current->nsproxy->mnt_ns);
	spin_unlock(&vfsmount_lock);
	chroot_fs_refs(&root, &new_nd.path);
	security_sb_post_pivotroot(&root, &new_nd.path);
	error = 0;
	path_put(&root_parent);
	path_put(&parent_path);
out2:
	mutex_unlock(&old_nd.path.dentry->d_inode->i_mutex);
	up_write(&namespace_sem);
	path_put(&root);
	path_put(&old_nd.path);
out1:
	path_put(&new_nd.path);
out0:
	return error;
out3:
	spin_unlock(&vfsmount_lock);
	goto out2;
}

static void __init init_mount_tree(void)
{
	struct vfsmount *mnt;
	struct mnt_namespace *ns;
	struct path root;

	mnt = do_kern_mount("rootfs", 0, "rootfs", NULL);
	if (IS_ERR(mnt))
		panic("Can't create rootfs");
	ns = kmalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns)
		panic("Can't allocate initial namespace");
	atomic_set(&ns->count, 1);
	INIT_LIST_HEAD(&ns->list);
	init_waitqueue_head(&ns->poll);
	ns->event = 0;
	list_add(&mnt->mnt_list, &ns->list);
	ns->root = mnt;
	mnt->mnt_ns = ns;

	init_task.nsproxy->mnt_ns = ns;
	get_mnt_ns(ns);

	root.mnt = ns->root;
	root.dentry = ns->root->mnt_root;

	set_fs_pwd(current->fs, &root);
	set_fs_root(current->fs, &root);
}

void __init mnt_init(void)
{
	unsigned u;
	int err;

	init_rwsem(&namespace_sem);

	mnt_cache = kmem_cache_create("mnt_cache", sizeof(struct vfsmount),
			0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, NULL);

	mount_hashtable = (struct list_head *)__get_free_page(GFP_ATOMIC);

	if (!mount_hashtable)
		panic("Failed to allocate mount hash table\n");

	printk("Mount-cache hash table entries: %lu\n", HASH_SIZE);

	for (u = 0; u < HASH_SIZE; u++)
		INIT_LIST_HEAD(&mount_hashtable[u]);

	err = sysfs_init();
	if (err)
		printk(KERN_WARNING "%s: sysfs_init error: %d\n",
			__FUNCTION__, err);
	fs_kobj = kobject_create_and_add("fs", NULL);
	if (!fs_kobj)
		printk(KERN_WARNING "%s: kobj create error\n", __FUNCTION__);
	init_rootfs();
	init_mount_tree();
}

void __put_mnt_ns(struct mnt_namespace *ns)
{
	struct vfsmount *root = ns->root;
	LIST_HEAD(umount_list);
	ns->root = NULL;
	spin_unlock(&vfsmount_lock);
	down_write(&namespace_sem);
	spin_lock(&vfsmount_lock);
	umount_tree(root, 0, &umount_list);
	spin_unlock(&vfsmount_lock);
	up_write(&namespace_sem);
	release_mounts(&umount_list);
	kfree(ns);
}
