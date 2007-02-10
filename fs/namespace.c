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
#include <linux/quotaops.h>
#include <linux/acct.h>
#include <linux/capability.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/seq_file.h>
#include <linux/mnt_namespace.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/ramfs.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include "pnode.h"

/* spinlock for vfsmount related operations, inplace of dcache_lock */
__cacheline_aligned_in_smp DEFINE_SPINLOCK(vfsmount_lock);

static int event;

static struct list_head *mount_hashtable __read_mostly;
static int hash_mask __read_mostly, hash_bits __read_mostly;
static struct kmem_cache *mnt_cache __read_mostly;
static struct rw_semaphore namespace_sem;

/* /sys/fs */
decl_subsys(fs, NULL, NULL);
EXPORT_SYMBOL_GPL(fs_subsys);

static inline unsigned long hash(struct vfsmount *mnt, struct dentry *dentry)
{
	unsigned long tmp = ((unsigned long)mnt / L1_CACHE_BYTES);
	tmp += ((unsigned long)dentry / L1_CACHE_BYTES);
	tmp = tmp + (tmp >> hash_bits);
	return tmp & hash_mask;
}

struct vfsmount *alloc_vfsmnt(const char *name)
{
	struct vfsmount *mnt = kmem_cache_zalloc(mnt_cache, GFP_KERNEL);
	if (mnt) {
		atomic_set(&mnt->mnt_count, 1);
		INIT_LIST_HEAD(&mnt->mnt_hash);
		INIT_LIST_HEAD(&mnt->mnt_child);
		INIT_LIST_HEAD(&mnt->mnt_mounts);
		INIT_LIST_HEAD(&mnt->mnt_list);
		INIT_LIST_HEAD(&mnt->mnt_expire);
		INIT_LIST_HEAD(&mnt->mnt_share);
		INIT_LIST_HEAD(&mnt->mnt_slave_list);
		INIT_LIST_HEAD(&mnt->mnt_slave);
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

static void detach_mnt(struct vfsmount *mnt, struct nameidata *old_nd)
{
	old_nd->dentry = mnt->mnt_mountpoint;
	old_nd->mnt = mnt->mnt_parent;
	mnt->mnt_parent = mnt;
	mnt->mnt_mountpoint = mnt->mnt_root;
	list_del_init(&mnt->mnt_child);
	list_del_init(&mnt->mnt_hash);
	old_nd->dentry->d_mounted--;
}

void mnt_set_mountpoint(struct vfsmount *mnt, struct dentry *dentry,
			struct vfsmount *child_mnt)
{
	child_mnt->mnt_parent = mntget(mnt);
	child_mnt->mnt_mountpoint = dget(dentry);
	dentry->d_mounted++;
}

static void attach_mnt(struct vfsmount *mnt, struct nameidata *nd)
{
	mnt_set_mountpoint(nd->mnt, nd->dentry, mnt);
	list_add_tail(&mnt->mnt_hash, mount_hashtable +
			hash(nd->mnt, nd->dentry));
	list_add_tail(&mnt->mnt_child, &nd->mnt->mnt_mounts);
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
		} else {
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
			spin_lock(&vfsmount_lock);
			if (!list_empty(&old->mnt_expire))
				list_add(&mnt->mnt_expire, &old->mnt_expire);
			spin_unlock(&vfsmount_lock);
		}
	}
	return mnt;
}

static inline void __mntput(struct vfsmount *mnt)
{
	struct super_block *sb = mnt->mnt_sb;
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

/* iterator */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct mnt_namespace *n = m->private;
	struct list_head *p;
	loff_t l = *pos;

	down_read(&namespace_sem);
	list_for_each(p, &n->list)
		if (!l--)
			return list_entry(p, struct vfsmount, mnt_list);
	return NULL;
}

static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct mnt_namespace *n = m->private;
	struct list_head *p = ((struct vfsmount *)v)->mnt_list.next;
	(*pos)++;
	return p == &n->list ? NULL : list_entry(p, struct vfsmount, mnt_list);
}

static void m_stop(struct seq_file *m, void *v)
{
	up_read(&namespace_sem);
}

static inline void mangle(struct seq_file *m, const char *s)
{
	seq_escape(m, s, " \t\n\\");
}

static int show_vfsmnt(struct seq_file *m, void *v)
{
	struct vfsmount *mnt = v;
	int err = 0;
	static struct proc_fs_info {
		int flag;
		char *str;
	} fs_info[] = {
		{ MS_SYNCHRONOUS, ",sync" },
		{ MS_DIRSYNC, ",dirsync" },
		{ MS_MANDLOCK, ",mand" },
		{ 0, NULL }
	};
	static struct proc_fs_info mnt_info[] = {
		{ MNT_NOSUID, ",nosuid" },
		{ MNT_NODEV, ",nodev" },
		{ MNT_NOEXEC, ",noexec" },
		{ MNT_NOATIME, ",noatime" },
		{ MNT_NODIRATIME, ",nodiratime" },
		{ MNT_RELATIME, ",relatime" },
		{ 0, NULL }
	};
	struct proc_fs_info *fs_infop;

	mangle(m, mnt->mnt_devname ? mnt->mnt_devname : "none");
	seq_putc(m, ' ');
	seq_path(m, mnt, mnt->mnt_root, " \t\n\\");
	seq_putc(m, ' ');
	mangle(m, mnt->mnt_sb->s_type->name);
	seq_puts(m, mnt->mnt_sb->s_flags & MS_RDONLY ? " ro" : " rw");
	for (fs_infop = fs_info; fs_infop->flag; fs_infop++) {
		if (mnt->mnt_sb->s_flags & fs_infop->flag)
			seq_puts(m, fs_infop->str);
	}
	for (fs_infop = mnt_info; fs_infop->flag; fs_infop++) {
		if (mnt->mnt_flags & fs_infop->flag)
			seq_puts(m, fs_infop->str);
	}
	if (mnt->mnt_sb->s_op->show_options)
		err = mnt->mnt_sb->s_op->show_options(m, mnt);
	seq_puts(m, " 0 0\n");
	return err;
}

struct seq_operations mounts_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_vfsmnt
};

static int show_vfsstat(struct seq_file *m, void *v)
{
	struct vfsmount *mnt = v;
	int err = 0;

	/* device */
	if (mnt->mnt_devname) {
		seq_puts(m, "device ");
		mangle(m, mnt->mnt_devname);
	} else
		seq_puts(m, "no device");

	/* mount point */
	seq_puts(m, " mounted on ");
	seq_path(m, mnt, mnt->mnt_root, " \t\n\\");
	seq_putc(m, ' ');

	/* file system type */
	seq_puts(m, "with fstype ");
	mangle(m, mnt->mnt_sb->s_type->name);

	/* optional statistics */
	if (mnt->mnt_sb->s_op->show_stats) {
		seq_putc(m, ' ');
		err = mnt->mnt_sb->s_op->show_stats(m, mnt);
	}

	seq_putc(m, '\n');
	return err;
}

struct seq_operations mountstats_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_vfsstat,
};

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
		mnt = list_entry(head->next, struct vfsmount, mnt_hash);
		list_del_init(&mnt->mnt_hash);
		if (mnt->mnt_parent != mnt) {
			struct dentry *dentry;
			struct vfsmount *m;
			spin_lock(&vfsmount_lock);
			dentry = mnt->mnt_mountpoint;
			m = mnt->mnt_parent;
			mnt->mnt_mountpoint = mnt->mnt_root;
			mnt->mnt_parent = mnt;
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
		if (p->mnt_parent != p)
			p->mnt_mountpoint->d_mounted--;
		change_mnt_propagation(p, MS_PRIVATE);
	}
}

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
		if (mnt == current->fs->rootmnt ||
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

	lock_kernel();
	if (sb->s_op->umount_begin)
		sb->s_op->umount_begin(mnt, flags);
	unlock_kernel();

	/*
	 * No sense to grab the lock for this test, but test itself looks
	 * somewhat bogus. Suggestions for better replacement?
	 * Ho-hum... In principle, we might treat that as umount + switch
	 * to rootfs. GC would eventually take care of the old vfsmount.
	 * Actually it makes sense, especially if rootfs would contain a
	 * /reboot - static binary that would close all descriptors and
	 * call reboot(9). Then init(8) could umount root and exec /reboot.
	 */
	if (mnt == current->fs->rootmnt && !(flags & MNT_DETACH)) {
		/*
		 * Special case for "unmounting" root ...
		 * we just try to remount it readonly.
		 */
		down_write(&sb->s_umount);
		if (!(sb->s_flags & MS_RDONLY)) {
			lock_kernel();
			DQUOT_OFF(sb);
			retval = do_remount_sb(sb, MS_RDONLY, NULL, 0);
			unlock_kernel();
		}
		up_write(&sb->s_umount);
		return retval;
	}

	down_write(&namespace_sem);
	spin_lock(&vfsmount_lock);
	event++;

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
	if (nd.dentry != nd.mnt->mnt_root)
		goto dput_and_out;
	if (!check_mnt(nd.mnt))
		goto dput_and_out;

	retval = -EPERM;
	if (!capable(CAP_SYS_ADMIN))
		goto dput_and_out;

	retval = do_umount(nd.mnt, flags);
dput_and_out:
	path_release_on_umount(&nd);
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
	if (S_ISLNK(nd->dentry->d_inode->i_mode))
		return -EPERM;
	if (nd->dentry->d_inode->i_mode & S_ISVTX) {
		if (current->uid != nd->dentry->d_inode->i_uid)
			return -EPERM;
	}
	if (vfs_permission(nd, MAY_WRITE))
		return -EPERM;
	return 0;
#endif
}

static int lives_below_in_same_fs(struct dentry *d, struct dentry *dentry)
{
	while (1) {
		if (d == dentry)
			return 1;
		if (d == NULL || d == d->d_parent)
			return 0;
		d = d->d_parent;
	}
}

struct vfsmount *copy_tree(struct vfsmount *mnt, struct dentry *dentry,
					int flag)
{
	struct vfsmount *res, *p, *q, *r, *s;
	struct nameidata nd;

	if (!(flag & CL_COPY_ALL) && IS_MNT_UNBINDABLE(mnt))
		return NULL;

	res = q = clone_mnt(mnt, dentry, flag);
	if (!q)
		goto Enomem;
	q->mnt_mountpoint = mnt->mnt_mountpoint;

	p = mnt;
	list_for_each_entry(r, &mnt->mnt_mounts, mnt_child) {
		if (!lives_below_in_same_fs(r->mnt_mountpoint, dentry))
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
			nd.mnt = q;
			nd.dentry = p->mnt_mountpoint;
			q = clone_mnt(p, p->mnt_root, flag);
			if (!q)
				goto Enomem;
			spin_lock(&vfsmount_lock);
			list_add_tail(&q->mnt_list, &res->mnt_list);
			attach_mnt(q, &nd);
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
			struct nameidata *nd, struct nameidata *parent_nd)
{
	LIST_HEAD(tree_list);
	struct vfsmount *dest_mnt = nd->mnt;
	struct dentry *dest_dentry = nd->dentry;
	struct vfsmount *child, *p;

	if (propagate_mnt(dest_mnt, dest_dentry, source_mnt, &tree_list))
		return -EINVAL;

	if (IS_MNT_SHARED(dest_mnt)) {
		for (p = source_mnt; p; p = next_mnt(p, source_mnt))
			set_mnt_shared(p);
	}

	spin_lock(&vfsmount_lock);
	if (parent_nd) {
		detach_mnt(source_mnt, parent_nd);
		attach_mnt(source_mnt, nd);
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
}

static int graft_tree(struct vfsmount *mnt, struct nameidata *nd)
{
	int err;
	if (mnt->mnt_sb->s_flags & MS_NOUSER)
		return -EINVAL;

	if (S_ISDIR(nd->dentry->d_inode->i_mode) !=
	      S_ISDIR(mnt->mnt_root->d_inode->i_mode))
		return -ENOTDIR;

	err = -ENOENT;
	mutex_lock(&nd->dentry->d_inode->i_mutex);
	if (IS_DEADDIR(nd->dentry->d_inode))
		goto out_unlock;

	err = security_sb_check_sb(mnt, nd);
	if (err)
		goto out_unlock;

	err = -ENOENT;
	if (IS_ROOT(nd->dentry) || !d_unhashed(nd->dentry))
		err = attach_recursive_mnt(mnt, nd, NULL);
out_unlock:
	mutex_unlock(&nd->dentry->d_inode->i_mutex);
	if (!err)
		security_sb_post_addmount(mnt, nd);
	return err;
}

/*
 * recursively change the type of the mountpoint.
 */
static int do_change_type(struct nameidata *nd, int flag)
{
	struct vfsmount *m, *mnt = nd->mnt;
	int recurse = flag & MS_REC;
	int type = flag & ~MS_REC;

	if (nd->dentry != nd->mnt->mnt_root)
		return -EINVAL;

	down_write(&namespace_sem);
	spin_lock(&vfsmount_lock);
	for (m = mnt; m; m = (recurse ? next_mnt(m, mnt) : NULL))
		change_mnt_propagation(m, type);
	spin_unlock(&vfsmount_lock);
	up_write(&namespace_sem);
	return 0;
}

/*
 * do loopback mount.
 */
static int do_loopback(struct nameidata *nd, char *old_name, int recurse)
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
	if (IS_MNT_UNBINDABLE(old_nd.mnt))
 		goto out;

	if (!check_mnt(nd->mnt) || !check_mnt(old_nd.mnt))
		goto out;

	err = -ENOMEM;
	if (recurse)
		mnt = copy_tree(old_nd.mnt, old_nd.dentry, 0);
	else
		mnt = clone_mnt(old_nd.mnt, old_nd.dentry, 0);

	if (!mnt)
		goto out;

	err = graft_tree(mnt, nd);
	if (err) {
		LIST_HEAD(umount_list);
		spin_lock(&vfsmount_lock);
		umount_tree(mnt, 0, &umount_list);
		spin_unlock(&vfsmount_lock);
		release_mounts(&umount_list);
	}

out:
	up_write(&namespace_sem);
	path_release(&old_nd);
	return err;
}

/*
 * change filesystem flags. dir should be a physical root of filesystem.
 * If you've mounted a non-root directory somewhere and want to do remount
 * on it - tough luck.
 */
static int do_remount(struct nameidata *nd, int flags, int mnt_flags,
		      void *data)
{
	int err;
	struct super_block *sb = nd->mnt->mnt_sb;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!check_mnt(nd->mnt))
		return -EINVAL;

	if (nd->dentry != nd->mnt->mnt_root)
		return -EINVAL;

	down_write(&sb->s_umount);
	err = do_remount_sb(sb, flags, data, 0);
	if (!err)
		nd->mnt->mnt_flags = mnt_flags;
	up_write(&sb->s_umount);
	if (!err)
		security_sb_post_remount(nd->mnt, flags, data);
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

static int do_move_mount(struct nameidata *nd, char *old_name)
{
	struct nameidata old_nd, parent_nd;
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
	while (d_mountpoint(nd->dentry) && follow_down(&nd->mnt, &nd->dentry))
		;
	err = -EINVAL;
	if (!check_mnt(nd->mnt) || !check_mnt(old_nd.mnt))
		goto out;

	err = -ENOENT;
	mutex_lock(&nd->dentry->d_inode->i_mutex);
	if (IS_DEADDIR(nd->dentry->d_inode))
		goto out1;

	if (!IS_ROOT(nd->dentry) && d_unhashed(nd->dentry))
		goto out1;

	err = -EINVAL;
	if (old_nd.dentry != old_nd.mnt->mnt_root)
		goto out1;

	if (old_nd.mnt == old_nd.mnt->mnt_parent)
		goto out1;

	if (S_ISDIR(nd->dentry->d_inode->i_mode) !=
	      S_ISDIR(old_nd.dentry->d_inode->i_mode))
		goto out1;
	/*
	 * Don't move a mount residing in a shared parent.
	 */
	if (old_nd.mnt->mnt_parent && IS_MNT_SHARED(old_nd.mnt->mnt_parent))
		goto out1;
	/*
	 * Don't move a mount tree containing unbindable mounts to a destination
	 * mount which is shared.
	 */
	if (IS_MNT_SHARED(nd->mnt) && tree_contains_unbindable(old_nd.mnt))
		goto out1;
	err = -ELOOP;
	for (p = nd->mnt; p->mnt_parent != p; p = p->mnt_parent)
		if (p == old_nd.mnt)
			goto out1;

	if ((err = attach_recursive_mnt(old_nd.mnt, nd, &parent_nd)))
		goto out1;

	spin_lock(&vfsmount_lock);
	/* if the mount is moved, it should no longer be expire
	 * automatically */
	list_del_init(&old_nd.mnt->mnt_expire);
	spin_unlock(&vfsmount_lock);
out1:
	mutex_unlock(&nd->dentry->d_inode->i_mutex);
out:
	up_write(&namespace_sem);
	if (!err)
		path_release(&parent_nd);
	path_release(&old_nd);
	return err;
}

/*
 * create a new mount for userspace and request it to be added into the
 * namespace's tree
 */
static int do_new_mount(struct nameidata *nd, char *type, int flags,
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
	while (d_mountpoint(nd->dentry) && follow_down(&nd->mnt, &nd->dentry))
		;
	err = -EINVAL;
	if (!check_mnt(nd->mnt))
		goto unlock;

	/* Refuse the same filesystem on the same mount point */
	err = -EBUSY;
	if (nd->mnt->mnt_sb == newmnt->mnt_sb &&
	    nd->mnt->mnt_root == nd->dentry)
		goto unlock;

	err = -EINVAL;
	if (S_ISLNK(newmnt->mnt_root->d_inode->i_mode))
		goto unlock;

	newmnt->mnt_flags = mnt_flags;
	if ((err = graft_tree(newmnt, nd)))
		goto unlock;

	if (fslist) {
		/* add to the specified expiration list */
		spin_lock(&vfsmount_lock);
		list_add_tail(&newmnt->mnt_expire, fslist);
		spin_unlock(&vfsmount_lock);
	}
	up_write(&namespace_sem);
	return 0;

unlock:
	up_write(&namespace_sem);
	mntput(newmnt);
	return err;
}

EXPORT_SYMBOL_GPL(do_add_mount);

static void expire_mount(struct vfsmount *mnt, struct list_head *mounts,
				struct list_head *umounts)
{
	spin_lock(&vfsmount_lock);

	/*
	 * Check if mount is still attached, if not, let whoever holds it deal
	 * with the sucker
	 */
	if (mnt->mnt_parent == mnt) {
		spin_unlock(&vfsmount_lock);
		return;
	}

	/*
	 * Check that it is still dead: the count should now be 2 - as
	 * contributed by the vfsmount parent and the mntget above
	 */
	if (!propagate_mount_busy(mnt, 2)) {
		/* delete from the namespace */
		touch_mnt_namespace(mnt->mnt_ns);
		list_del_init(&mnt->mnt_list);
		mnt->mnt_ns = NULL;
		umount_tree(mnt, 1, umounts);
		spin_unlock(&vfsmount_lock);
	} else {
		/*
		 * Someone brought it back to life whilst we didn't have any
		 * locks held so return it to the expiration list
		 */
		list_add_tail(&mnt->mnt_expire, mounts);
		spin_unlock(&vfsmount_lock);
	}
}

/*
 * go through the vfsmounts we've just consigned to the graveyard to
 * - check that they're still dead
 * - delete the vfsmount from the appropriate namespace under lock
 * - dispose of the corpse
 */
static void expire_mount_list(struct list_head *graveyard, struct list_head *mounts)
{
	struct mnt_namespace *ns;
	struct vfsmount *mnt;

	while (!list_empty(graveyard)) {
		LIST_HEAD(umounts);
		mnt = list_entry(graveyard->next, struct vfsmount, mnt_expire);
		list_del_init(&mnt->mnt_expire);

		/* don't do anything if the namespace is dead - all the
		 * vfsmounts from it are going away anyway */
		ns = mnt->mnt_ns;
		if (!ns || !ns->root)
			continue;
		get_mnt_ns(ns);

		spin_unlock(&vfsmount_lock);
		down_write(&namespace_sem);
		expire_mount(mnt, mounts, &umounts);
		up_write(&namespace_sem);
		release_mounts(&umounts);
		mntput(mnt);
		put_mnt_ns(ns);
		spin_lock(&vfsmount_lock);
	}
}

/*
 * process a list of expirable mountpoints with the intent of discarding any
 * mountpoints that aren't in use and haven't been touched since last we came
 * here
 */
void mark_mounts_for_expiry(struct list_head *mounts)
{
	struct vfsmount *mnt, *next;
	LIST_HEAD(graveyard);

	if (list_empty(mounts))
		return;

	spin_lock(&vfsmount_lock);

	/* extract from the expiration list every vfsmount that matches the
	 * following criteria:
	 * - only referenced by its parent vfsmount
	 * - still marked for expiry (marked on the last call here; marks are
	 *   cleared by mntput())
	 */
	list_for_each_entry_safe(mnt, next, mounts, mnt_expire) {
		if (!xchg(&mnt->mnt_expiry_mark, 1) ||
		    atomic_read(&mnt->mnt_count) != 1)
			continue;

		mntget(mnt);
		list_move(&mnt->mnt_expire, &graveyard);
	}

	expire_mount_list(&graveyard, mounts);

	spin_unlock(&vfsmount_lock);
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
			mntget(mnt);
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
void shrink_submounts(struct vfsmount *mountpoint, struct list_head *mounts)
{
	LIST_HEAD(graveyard);
	int found;

	spin_lock(&vfsmount_lock);

	/* extract submounts of 'mountpoint' from the expiration list */
	while ((found = select_submounts(mountpoint, &graveyard)) != 0)
		expire_mount_list(&graveyard, mounts);

	spin_unlock(&vfsmount_lock);
}

EXPORT_SYMBOL_GPL(shrink_submounts);

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

	flags &= ~(MS_NOSUID | MS_NOEXEC | MS_NODEV | MS_ACTIVE |
		   MS_NOATIME | MS_NODIRATIME | MS_RELATIME);

	/* ... and get the mountpoint */
	retval = path_lookup(dir_name, LOOKUP_FOLLOW, &nd);
	if (retval)
		return retval;

	retval = security_sb_mount(dev_name, &nd, type_page, flags, data_page);
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
	path_release(&nd);
	return retval;
}

/*
 * Allocate a new namespace structure and populate it with contents
 * copied from the namespace of the passed in task structure.
 */
struct mnt_namespace *dup_mnt_ns(struct task_struct *tsk,
		struct fs_struct *fs)
{
	struct mnt_namespace *mnt_ns = tsk->nsproxy->mnt_ns;
	struct mnt_namespace *new_ns;
	struct vfsmount *rootmnt = NULL, *pwdmnt = NULL, *altrootmnt = NULL;
	struct vfsmount *p, *q;

	new_ns = kmalloc(sizeof(struct mnt_namespace), GFP_KERNEL);
	if (!new_ns)
		return NULL;

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
		return NULL;
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
			if (p == fs->rootmnt) {
				rootmnt = p;
				fs->rootmnt = mntget(q);
			}
			if (p == fs->pwdmnt) {
				pwdmnt = p;
				fs->pwdmnt = mntget(q);
			}
			if (p == fs->altrootmnt) {
				altrootmnt = p;
				fs->altrootmnt = mntget(q);
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

int copy_mnt_ns(int flags, struct task_struct *tsk)
{
	struct mnt_namespace *ns = tsk->nsproxy->mnt_ns;
	struct mnt_namespace *new_ns;
	int err = 0;

	if (!ns)
		return 0;

	get_mnt_ns(ns);

	if (!(flags & CLONE_NEWNS))
		return 0;

	if (!capable(CAP_SYS_ADMIN)) {
		err = -EPERM;
		goto out;
	}

	new_ns = dup_mnt_ns(tsk, tsk->fs);
	if (!new_ns) {
		err = -ENOMEM;
		goto out;
	}

	tsk->nsproxy->mnt_ns = new_ns;

out:
	put_mnt_ns(ns);
	return err;
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
void set_fs_root(struct fs_struct *fs, struct vfsmount *mnt,
		 struct dentry *dentry)
{
	struct dentry *old_root;
	struct vfsmount *old_rootmnt;
	write_lock(&fs->lock);
	old_root = fs->root;
	old_rootmnt = fs->rootmnt;
	fs->rootmnt = mntget(mnt);
	fs->root = dget(dentry);
	write_unlock(&fs->lock);
	if (old_root) {
		dput(old_root);
		mntput(old_rootmnt);
	}
}

/*
 * Replace the fs->{pwdmnt,pwd} with {mnt,dentry}. Put the old values.
 * It can block. Requires the big lock held.
 */
void set_fs_pwd(struct fs_struct *fs, struct vfsmount *mnt,
		struct dentry *dentry)
{
	struct dentry *old_pwd;
	struct vfsmount *old_pwdmnt;

	write_lock(&fs->lock);
	old_pwd = fs->pwd;
	old_pwdmnt = fs->pwdmnt;
	fs->pwdmnt = mntget(mnt);
	fs->pwd = dget(dentry);
	write_unlock(&fs->lock);

	if (old_pwd) {
		dput(old_pwd);
		mntput(old_pwdmnt);
	}
}

static void chroot_fs_refs(struct nameidata *old_nd, struct nameidata *new_nd)
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
			if (fs->root == old_nd->dentry
			    && fs->rootmnt == old_nd->mnt)
				set_fs_root(fs, new_nd->mnt, new_nd->dentry);
			if (fs->pwd == old_nd->dentry
			    && fs->pwdmnt == old_nd->mnt)
				set_fs_pwd(fs, new_nd->mnt, new_nd->dentry);
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
	struct nameidata new_nd, old_nd, parent_nd, root_parent, user_nd;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	lock_kernel();

	error = __user_walk(new_root, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
			    &new_nd);
	if (error)
		goto out0;
	error = -EINVAL;
	if (!check_mnt(new_nd.mnt))
		goto out1;

	error = __user_walk(put_old, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &old_nd);
	if (error)
		goto out1;

	error = security_sb_pivotroot(&old_nd, &new_nd);
	if (error) {
		path_release(&old_nd);
		goto out1;
	}

	read_lock(&current->fs->lock);
	user_nd.mnt = mntget(current->fs->rootmnt);
	user_nd.dentry = dget(current->fs->root);
	read_unlock(&current->fs->lock);
	down_write(&namespace_sem);
	mutex_lock(&old_nd.dentry->d_inode->i_mutex);
	error = -EINVAL;
	if (IS_MNT_SHARED(old_nd.mnt) ||
		IS_MNT_SHARED(new_nd.mnt->mnt_parent) ||
		IS_MNT_SHARED(user_nd.mnt->mnt_parent))
		goto out2;
	if (!check_mnt(user_nd.mnt))
		goto out2;
	error = -ENOENT;
	if (IS_DEADDIR(new_nd.dentry->d_inode))
		goto out2;
	if (d_unhashed(new_nd.dentry) && !IS_ROOT(new_nd.dentry))
		goto out2;
	if (d_unhashed(old_nd.dentry) && !IS_ROOT(old_nd.dentry))
		goto out2;
	error = -EBUSY;
	if (new_nd.mnt == user_nd.mnt || old_nd.mnt == user_nd.mnt)
		goto out2; /* loop, on the same file system  */
	error = -EINVAL;
	if (user_nd.mnt->mnt_root != user_nd.dentry)
		goto out2; /* not a mountpoint */
	if (user_nd.mnt->mnt_parent == user_nd.mnt)
		goto out2; /* not attached */
	if (new_nd.mnt->mnt_root != new_nd.dentry)
		goto out2; /* not a mountpoint */
	if (new_nd.mnt->mnt_parent == new_nd.mnt)
		goto out2; /* not attached */
	tmp = old_nd.mnt; /* make sure we can reach put_old from new_root */
	spin_lock(&vfsmount_lock);
	if (tmp != new_nd.mnt) {
		for (;;) {
			if (tmp->mnt_parent == tmp)
				goto out3; /* already mounted on put_old */
			if (tmp->mnt_parent == new_nd.mnt)
				break;
			tmp = tmp->mnt_parent;
		}
		if (!is_subdir(tmp->mnt_mountpoint, new_nd.dentry))
			goto out3;
	} else if (!is_subdir(old_nd.dentry, new_nd.dentry))
		goto out3;
	detach_mnt(new_nd.mnt, &parent_nd);
	detach_mnt(user_nd.mnt, &root_parent);
	attach_mnt(user_nd.mnt, &old_nd);     /* mount old root on put_old */
	attach_mnt(new_nd.mnt, &root_parent); /* mount new_root on / */
	touch_mnt_namespace(current->nsproxy->mnt_ns);
	spin_unlock(&vfsmount_lock);
	chroot_fs_refs(&user_nd, &new_nd);
	security_sb_post_pivotroot(&user_nd, &new_nd);
	error = 0;
	path_release(&root_parent);
	path_release(&parent_nd);
out2:
	mutex_unlock(&old_nd.dentry->d_inode->i_mutex);
	up_write(&namespace_sem);
	path_release(&user_nd);
	path_release(&old_nd);
out1:
	path_release(&new_nd);
out0:
	unlock_kernel();
	return error;
out3:
	spin_unlock(&vfsmount_lock);
	goto out2;
}

static void __init init_mount_tree(void)
{
	struct vfsmount *mnt;
	struct mnt_namespace *ns;

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

	set_fs_pwd(current->fs, ns->root, ns->root->mnt_root);
	set_fs_root(current->fs, ns->root, ns->root->mnt_root);
}

void __init mnt_init(unsigned long mempages)
{
	struct list_head *d;
	unsigned int nr_hash;
	int i;
	int err;

	init_rwsem(&namespace_sem);

	mnt_cache = kmem_cache_create("mnt_cache", sizeof(struct vfsmount),
			0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, NULL, NULL);

	mount_hashtable = (struct list_head *)__get_free_page(GFP_ATOMIC);

	if (!mount_hashtable)
		panic("Failed to allocate mount hash table\n");

	/*
	 * Find the power-of-two list-heads that can fit into the allocation..
	 * We don't guarantee that "sizeof(struct list_head)" is necessarily
	 * a power-of-two.
	 */
	nr_hash = PAGE_SIZE / sizeof(struct list_head);
	hash_bits = 0;
	do {
		hash_bits++;
	} while ((nr_hash >> hash_bits) != 0);
	hash_bits--;

	/*
	 * Re-calculate the actual number of entries and the mask
	 * from the number of bits we can fit.
	 */
	nr_hash = 1UL << hash_bits;
	hash_mask = nr_hash - 1;

	printk("Mount-cache hash table entries: %d\n", nr_hash);

	/* And initialize the newly allocated array */
	d = mount_hashtable;
	i = nr_hash;
	do {
		INIT_LIST_HEAD(d);
		d++;
		i--;
	} while (i);
	err = sysfs_init();
	if (err)
		printk(KERN_WARNING "%s: sysfs_init error: %d\n",
			__FUNCTION__, err);
	err = subsystem_register(&fs_subsys);
	if (err)
		printk(KERN_WARNING "%s: subsystem_register error: %d\n",
			__FUNCTION__, err);
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
