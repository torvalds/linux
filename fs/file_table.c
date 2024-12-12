// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/file_table.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/security.h>
#include <linux/cred.h>
#include <linux/eventpoll.h>
#include <linux/rcupdate.h>
#include <linux/mount.h>
#include <linux/capability.h>
#include <linux/cdev.h>
#include <linux/fsnotify.h>
#include <linux/sysctl.h>
#include <linux/percpu_counter.h>
#include <linux/percpu.h>
#include <linux/task_work.h>
#include <linux/swap.h>
#include <linux/kmemleak.h>

#include <linux/atomic.h>

#include "internal.h"

/* sysctl tunables... */
static struct files_stat_struct files_stat = {
	.max_files = NR_FILE
};

/* SLAB cache for file structures */
static struct kmem_cache *filp_cachep __ro_after_init;
static struct kmem_cache *bfilp_cachep __ro_after_init;

static struct percpu_counter nr_files __cacheline_aligned_in_smp;

/* Container for backing file with optional user path */
struct backing_file {
	struct file file;
	union {
		struct path user_path;
		freeptr_t bf_freeptr;
	};
};

static inline struct backing_file *backing_file(struct file *f)
{
	return container_of(f, struct backing_file, file);
}

struct path *backing_file_user_path(struct file *f)
{
	return &backing_file(f)->user_path;
}
EXPORT_SYMBOL_GPL(backing_file_user_path);

static inline void file_free(struct file *f)
{
	security_file_free(f);
	if (likely(!(f->f_mode & FMODE_NOACCOUNT)))
		percpu_counter_dec(&nr_files);
	put_cred(f->f_cred);
	if (unlikely(f->f_mode & FMODE_BACKING)) {
		path_put(backing_file_user_path(f));
		kmem_cache_free(bfilp_cachep, backing_file(f));
	} else {
		kmem_cache_free(filp_cachep, f);
	}
}

/*
 * Return the total number of open files in the system
 */
static long get_nr_files(void)
{
	return percpu_counter_read_positive(&nr_files);
}

/*
 * Return the maximum number of open files in the system
 */
unsigned long get_max_files(void)
{
	return files_stat.max_files;
}
EXPORT_SYMBOL_GPL(get_max_files);

#if defined(CONFIG_SYSCTL) && defined(CONFIG_PROC_FS)

/*
 * Handle nr_files sysctl
 */
static int proc_nr_files(const struct ctl_table *table, int write, void *buffer,
			 size_t *lenp, loff_t *ppos)
{
	files_stat.nr_files = get_nr_files();
	return proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
}

static struct ctl_table fs_stat_sysctls[] = {
	{
		.procname	= "file-nr",
		.data		= &files_stat,
		.maxlen		= sizeof(files_stat),
		.mode		= 0444,
		.proc_handler	= proc_nr_files,
	},
	{
		.procname	= "file-max",
		.data		= &files_stat.max_files,
		.maxlen		= sizeof(files_stat.max_files),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= SYSCTL_LONG_ZERO,
		.extra2		= SYSCTL_LONG_MAX,
	},
	{
		.procname	= "nr_open",
		.data		= &sysctl_nr_open,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &sysctl_nr_open_min,
		.extra2		= &sysctl_nr_open_max,
	},
};

static int __init init_fs_stat_sysctls(void)
{
	register_sysctl_init("fs", fs_stat_sysctls);
	if (IS_ENABLED(CONFIG_BINFMT_MISC)) {
		struct ctl_table_header *hdr;

		hdr = register_sysctl_mount_point("fs/binfmt_misc");
		kmemleak_not_leak(hdr);
	}
	return 0;
}
fs_initcall(init_fs_stat_sysctls);
#endif

static int init_file(struct file *f, int flags, const struct cred *cred)
{
	int error;

	f->f_cred = get_cred(cred);
	error = security_file_alloc(f);
	if (unlikely(error)) {
		put_cred(f->f_cred);
		return error;
	}

	spin_lock_init(&f->f_lock);
	/*
	 * Note that f_pos_lock is only used for files raising
	 * FMODE_ATOMIC_POS and directories. Other files such as pipes
	 * don't need it and since f_pos_lock is in a union may reuse
	 * the space for other purposes. They are expected to initialize
	 * the respective member when opening the file.
	 */
	mutex_init(&f->f_pos_lock);
	memset(&f->f_path, 0, sizeof(f->f_path));
	memset(&f->f_ra, 0, sizeof(f->f_ra));

	f->f_flags	= flags;
	f->f_mode	= OPEN_FMODE(flags);

	f->f_op		= NULL;
	f->f_mapping	= NULL;
	f->private_data = NULL;
	f->f_inode	= NULL;
	f->f_owner	= NULL;
#ifdef CONFIG_EPOLL
	f->f_ep		= NULL;
#endif

	f->f_iocb_flags = 0;
	f->f_pos	= 0;
	f->f_wb_err	= 0;
	f->f_sb_err	= 0;

	/*
	 * We're SLAB_TYPESAFE_BY_RCU so initialize f_count last. While
	 * fget-rcu pattern users need to be able to handle spurious
	 * refcount bumps we should reinitialize the reused file first.
	 */
	file_ref_init(&f->f_ref, 1);
	return 0;
}

/* Find an unused file structure and return a pointer to it.
 * Returns an error pointer if some error happend e.g. we over file
 * structures limit, run out of memory or operation is not permitted.
 *
 * Be very careful using this.  You are responsible for
 * getting write access to any mount that you might assign
 * to this filp, if it is opened for write.  If this is not
 * done, you will imbalance int the mount's writer count
 * and a warning at __fput() time.
 */
struct file *alloc_empty_file(int flags, const struct cred *cred)
{
	static long old_max;
	struct file *f;
	int error;

	/*
	 * Privileged users can go above max_files
	 */
	if (get_nr_files() >= files_stat.max_files && !capable(CAP_SYS_ADMIN)) {
		/*
		 * percpu_counters are inaccurate.  Do an expensive check before
		 * we go and fail.
		 */
		if (percpu_counter_sum_positive(&nr_files) >= files_stat.max_files)
			goto over;
	}

	f = kmem_cache_alloc(filp_cachep, GFP_KERNEL);
	if (unlikely(!f))
		return ERR_PTR(-ENOMEM);

	error = init_file(f, flags, cred);
	if (unlikely(error)) {
		kmem_cache_free(filp_cachep, f);
		return ERR_PTR(error);
	}

	percpu_counter_inc(&nr_files);

	return f;

over:
	/* Ran out of filps - report that */
	if (get_nr_files() > old_max) {
		pr_info("VFS: file-max limit %lu reached\n", get_max_files());
		old_max = get_nr_files();
	}
	return ERR_PTR(-ENFILE);
}

/*
 * Variant of alloc_empty_file() that doesn't check and modify nr_files.
 *
 * This is only for kernel internal use, and the allocate file must not be
 * installed into file tables or such.
 */
struct file *alloc_empty_file_noaccount(int flags, const struct cred *cred)
{
	struct file *f;
	int error;

	f = kmem_cache_alloc(filp_cachep, GFP_KERNEL);
	if (unlikely(!f))
		return ERR_PTR(-ENOMEM);

	error = init_file(f, flags, cred);
	if (unlikely(error)) {
		kmem_cache_free(filp_cachep, f);
		return ERR_PTR(error);
	}

	f->f_mode |= FMODE_NOACCOUNT;

	return f;
}

/*
 * Variant of alloc_empty_file() that allocates a backing_file container
 * and doesn't check and modify nr_files.
 *
 * This is only for kernel internal use, and the allocate file must not be
 * installed into file tables or such.
 */
struct file *alloc_empty_backing_file(int flags, const struct cred *cred)
{
	struct backing_file *ff;
	int error;

	ff = kmem_cache_alloc(bfilp_cachep, GFP_KERNEL);
	if (unlikely(!ff))
		return ERR_PTR(-ENOMEM);

	error = init_file(&ff->file, flags, cred);
	if (unlikely(error)) {
		kmem_cache_free(bfilp_cachep, ff);
		return ERR_PTR(error);
	}

	ff->file.f_mode |= FMODE_BACKING | FMODE_NOACCOUNT;
	return &ff->file;
}

/**
 * file_init_path - initialize a 'struct file' based on path
 *
 * @file: the file to set up
 * @path: the (dentry, vfsmount) pair for the new file
 * @fop: the 'struct file_operations' for the new file
 */
static void file_init_path(struct file *file, const struct path *path,
			   const struct file_operations *fop)
{
	file->f_path = *path;
	file->f_inode = path->dentry->d_inode;
	file->f_mapping = path->dentry->d_inode->i_mapping;
	file->f_wb_err = filemap_sample_wb_err(file->f_mapping);
	file->f_sb_err = file_sample_sb_err(file);
	if (fop->llseek)
		file->f_mode |= FMODE_LSEEK;
	if ((file->f_mode & FMODE_READ) &&
	     likely(fop->read || fop->read_iter))
		file->f_mode |= FMODE_CAN_READ;
	if ((file->f_mode & FMODE_WRITE) &&
	     likely(fop->write || fop->write_iter))
		file->f_mode |= FMODE_CAN_WRITE;
	file->f_iocb_flags = iocb_flags(file);
	file->f_mode |= FMODE_OPENED;
	file->f_op = fop;
	if ((file->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		i_readcount_inc(path->dentry->d_inode);
}

/**
 * alloc_file - allocate and initialize a 'struct file'
 *
 * @path: the (dentry, vfsmount) pair for the new file
 * @flags: O_... flags with which the new file will be opened
 * @fop: the 'struct file_operations' for the new file
 */
static struct file *alloc_file(const struct path *path, int flags,
		const struct file_operations *fop)
{
	struct file *file;

	file = alloc_empty_file(flags, current_cred());
	if (!IS_ERR(file))
		file_init_path(file, path, fop);
	return file;
}

static inline int alloc_path_pseudo(const char *name, struct inode *inode,
				    struct vfsmount *mnt, struct path *path)
{
	struct qstr this = QSTR_INIT(name, strlen(name));

	path->dentry = d_alloc_pseudo(mnt->mnt_sb, &this);
	if (!path->dentry)
		return -ENOMEM;
	path->mnt = mntget(mnt);
	d_instantiate(path->dentry, inode);
	return 0;
}

struct file *alloc_file_pseudo(struct inode *inode, struct vfsmount *mnt,
			       const char *name, int flags,
			       const struct file_operations *fops)
{
	int ret;
	struct path path;
	struct file *file;

	ret = alloc_path_pseudo(name, inode, mnt, &path);
	if (ret)
		return ERR_PTR(ret);

	file = alloc_file(&path, flags, fops);
	if (IS_ERR(file)) {
		ihold(inode);
		path_put(&path);
	}
	return file;
}
EXPORT_SYMBOL(alloc_file_pseudo);

struct file *alloc_file_pseudo_noaccount(struct inode *inode,
					 struct vfsmount *mnt, const char *name,
					 int flags,
					 const struct file_operations *fops)
{
	int ret;
	struct path path;
	struct file *file;

	ret = alloc_path_pseudo(name, inode, mnt, &path);
	if (ret)
		return ERR_PTR(ret);

	file = alloc_empty_file_noaccount(flags, current_cred());
	if (IS_ERR(file)) {
		ihold(inode);
		path_put(&path);
		return file;
	}
	file_init_path(file, &path, fops);
	return file;
}
EXPORT_SYMBOL_GPL(alloc_file_pseudo_noaccount);

struct file *alloc_file_clone(struct file *base, int flags,
				const struct file_operations *fops)
{
	struct file *f;

	f = alloc_file(&base->f_path, flags, fops);
	if (!IS_ERR(f)) {
		path_get(&f->f_path);
		f->f_mapping = base->f_mapping;
	}
	return f;
}

/* the real guts of fput() - releasing the last reference to file
 */
static void __fput(struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct vfsmount *mnt = file->f_path.mnt;
	struct inode *inode = file->f_inode;
	fmode_t mode = file->f_mode;

	if (unlikely(!(file->f_mode & FMODE_OPENED)))
		goto out;

	might_sleep();

	fsnotify_close(file);
	/*
	 * The function eventpoll_release() should be the first called
	 * in the file cleanup chain.
	 */
	eventpoll_release(file);
	locks_remove_file(file);

	security_file_release(file);
	if (unlikely(file->f_flags & FASYNC)) {
		if (file->f_op->fasync)
			file->f_op->fasync(-1, file, 0);
	}
	if (file->f_op->release)
		file->f_op->release(inode, file);
	if (unlikely(S_ISCHR(inode->i_mode) && inode->i_cdev != NULL &&
		     !(mode & FMODE_PATH))) {
		cdev_put(inode->i_cdev);
	}
	fops_put(file->f_op);
	file_f_owner_release(file);
	put_file_access(file);
	dput(dentry);
	if (unlikely(mode & FMODE_NEED_UNMOUNT))
		dissolve_on_fput(mnt);
	mntput(mnt);
out:
	file_free(file);
}

static LLIST_HEAD(delayed_fput_list);
static void delayed_fput(struct work_struct *unused)
{
	struct llist_node *node = llist_del_all(&delayed_fput_list);
	struct file *f, *t;

	llist_for_each_entry_safe(f, t, node, f_llist)
		__fput(f);
}

static void ____fput(struct callback_head *work)
{
	__fput(container_of(work, struct file, f_task_work));
}

/*
 * If kernel thread really needs to have the final fput() it has done
 * to complete, call this.  The only user right now is the boot - we
 * *do* need to make sure our writes to binaries on initramfs has
 * not left us with opened struct file waiting for __fput() - execve()
 * won't work without that.  Please, don't add more callers without
 * very good reasons; in particular, never call that with locks
 * held and never call that from a thread that might need to do
 * some work on any kind of umount.
 */
void flush_delayed_fput(void)
{
	delayed_fput(NULL);
}
EXPORT_SYMBOL_GPL(flush_delayed_fput);

static DECLARE_DELAYED_WORK(delayed_fput_work, delayed_fput);

void fput(struct file *file)
{
	if (file_ref_put(&file->f_ref)) {
		struct task_struct *task = current;

		if (unlikely(!(file->f_mode & (FMODE_BACKING | FMODE_OPENED)))) {
			file_free(file);
			return;
		}
		if (likely(!in_interrupt() && !(task->flags & PF_KTHREAD))) {
			init_task_work(&file->f_task_work, ____fput);
			if (!task_work_add(task, &file->f_task_work, TWA_RESUME))
				return;
			/*
			 * After this task has run exit_task_work(),
			 * task_work_add() will fail.  Fall through to delayed
			 * fput to avoid leaking *file.
			 */
		}

		if (llist_add(&file->f_llist, &delayed_fput_list))
			schedule_delayed_work(&delayed_fput_work, 1);
	}
}

/*
 * synchronous analog of fput(); for kernel threads that might be needed
 * in some umount() (and thus can't use flush_delayed_fput() without
 * risking deadlocks), need to wait for completion of __fput() and know
 * for this specific struct file it won't involve anything that would
 * need them.  Use only if you really need it - at the very least,
 * don't blindly convert fput() by kernel thread to that.
 */
void __fput_sync(struct file *file)
{
	if (file_ref_put(&file->f_ref))
		__fput(file);
}

EXPORT_SYMBOL(fput);
EXPORT_SYMBOL(__fput_sync);

void __init files_init(void)
{
	struct kmem_cache_args args = {
		.use_freeptr_offset = true,
		.freeptr_offset = offsetof(struct file, f_freeptr),
	};

	filp_cachep = kmem_cache_create("filp", sizeof(struct file), &args,
				SLAB_HWCACHE_ALIGN | SLAB_PANIC |
				SLAB_ACCOUNT | SLAB_TYPESAFE_BY_RCU);

	args.freeptr_offset = offsetof(struct backing_file, bf_freeptr);
	bfilp_cachep = kmem_cache_create("bfilp", sizeof(struct backing_file),
				&args, SLAB_HWCACHE_ALIGN | SLAB_PANIC |
				SLAB_ACCOUNT | SLAB_TYPESAFE_BY_RCU);
	percpu_counter_init(&nr_files, 0, GFP_KERNEL);
}

/*
 * One file with associated inode and dcache is very roughly 1K. Per default
 * do not use more than 10% of our memory for files.
 */
void __init files_maxfiles_init(void)
{
	unsigned long n;
	unsigned long nr_pages = totalram_pages();
	unsigned long memreserve = (nr_pages - nr_free_pages()) * 3/2;

	memreserve = min(memreserve, nr_pages - 1);
	n = ((nr_pages - memreserve) * (PAGE_SIZE / 1024)) / 10;

	files_stat.max_files = max_t(unsigned long, n, NR_FILE);
}
