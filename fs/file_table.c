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
#include <linux/security.h>
#include <linux/eventpoll.h>
#include <linux/rcupdate.h>
#include <linux/mount.h>
#include <linux/capability.h>
#include <linux/cdev.h>
#include <linux/fsnotify.h>
#include <linux/sysctl.h>
#include <linux/percpu_counter.h>

#include <asm/atomic.h>

/* sysctl tunables... */
struct files_stat_struct files_stat = {
	.max_files = NR_FILE
};

/* public. Not pretty! */
__cacheline_aligned_in_smp DEFINE_SPINLOCK(files_lock);

static struct percpu_counter nr_files __cacheline_aligned_in_smp;

static inline void file_free_rcu(struct rcu_head *head)
{
	struct file *f =  container_of(head, struct file, f_u.fu_rcuhead);
	kmem_cache_free(filp_cachep, f);
}

static inline void file_free(struct file *f)
{
	percpu_counter_dec(&nr_files);
	call_rcu(&f->f_u.fu_rcuhead, file_free_rcu);
}

/*
 * Return the total number of open files in the system
 */
static int get_nr_files(void)
{
	return percpu_counter_read_positive(&nr_files);
}

/*
 * Return the maximum number of open files in the system
 */
int get_max_files(void)
{
	return files_stat.max_files;
}
EXPORT_SYMBOL_GPL(get_max_files);

/*
 * Handle nr_files sysctl
 */
#if defined(CONFIG_SYSCTL) && defined(CONFIG_PROC_FS)
int proc_nr_files(ctl_table *table, int write, struct file *filp,
                     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	files_stat.nr_files = get_nr_files();
	return proc_dointvec(table, write, filp, buffer, lenp, ppos);
}
#else
int proc_nr_files(ctl_table *table, int write, struct file *filp,
                     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}
#endif

/* Find an unused file structure and return a pointer to it.
 * Returns NULL, if there are no more free file structures or
 * we run out of memory.
 *
 * Be very careful using this.  You are responsible for
 * getting write access to any mount that you might assign
 * to this filp, if it is opened for write.  If this is not
 * done, you will imbalance int the mount's writer count
 * and a warning at __fput() time.
 */
struct file *get_empty_filp(void)
{
	struct task_struct *tsk;
	static int old_max;
	struct file * f;

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

	f = kmem_cache_zalloc(filp_cachep, GFP_KERNEL);
	if (f == NULL)
		goto fail;

	percpu_counter_inc(&nr_files);
	if (security_file_alloc(f))
		goto fail_sec;

	tsk = current;
	INIT_LIST_HEAD(&f->f_u.fu_list);
	atomic_set(&f->f_count, 1);
	rwlock_init(&f->f_owner.lock);
	f->f_uid = tsk->fsuid;
	f->f_gid = tsk->fsgid;
	eventpoll_init_file(f);
	/* f->f_version: 0 */
	return f;

over:
	/* Ran out of filps - report that */
	if (get_nr_files() > old_max) {
		printk(KERN_INFO "VFS: file-max limit %d reached\n",
					get_max_files());
		old_max = get_nr_files();
	}
	goto fail;

fail_sec:
	file_free(f);
fail:
	return NULL;
}

EXPORT_SYMBOL(get_empty_filp);

/**
 * alloc_file - allocate and initialize a 'struct file'
 * @mnt: the vfsmount on which the file will reside
 * @dentry: the dentry representing the new file
 * @mode: the mode with which the new file will be opened
 * @fop: the 'struct file_operations' for the new file
 *
 * Use this instead of get_empty_filp() to get a new
 * 'struct file'.  Do so because of the same initialization
 * pitfalls reasons listed for init_file().  This is a
 * preferred interface to using init_file().
 *
 * If all the callers of init_file() are eliminated, its
 * code should be moved into this function.
 */
struct file *alloc_file(struct vfsmount *mnt, struct dentry *dentry,
		mode_t mode, const struct file_operations *fop)
{
	struct file *file;
	struct path;

	file = get_empty_filp();
	if (!file)
		return NULL;

	init_file(file, mnt, dentry, mode, fop);
	return file;
}
EXPORT_SYMBOL(alloc_file);

/**
 * init_file - initialize a 'struct file'
 * @file: the already allocated 'struct file' to initialized
 * @mnt: the vfsmount on which the file resides
 * @dentry: the dentry representing this file
 * @mode: the mode the file is opened with
 * @fop: the 'struct file_operations' for this file
 *
 * Use this instead of setting the members directly.  Doing so
 * avoids making mistakes like forgetting the mntget() or
 * forgetting to take a write on the mnt.
 *
 * Note: This is a crappy interface.  It is here to make
 * merging with the existing users of get_empty_filp()
 * who have complex failure logic easier.  All users
 * of this should be moving to alloc_file().
 */
int init_file(struct file *file, struct vfsmount *mnt, struct dentry *dentry,
	   mode_t mode, const struct file_operations *fop)
{
	int error = 0;
	file->f_path.dentry = dentry;
	file->f_path.mnt = mntget(mnt);
	file->f_mapping = dentry->d_inode->i_mapping;
	file->f_mode = mode;
	file->f_op = fop;
	return error;
}
EXPORT_SYMBOL(init_file);

void fput(struct file *file)
{
	if (atomic_dec_and_test(&file->f_count))
		__fput(file);
}

EXPORT_SYMBOL(fput);

/* __fput is called from task context when aio completion releases the last
 * last use of a struct file *.  Do not use otherwise.
 */
void __fput(struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct vfsmount *mnt = file->f_path.mnt;
	struct inode *inode = dentry->d_inode;

	might_sleep();

	fsnotify_close(file);
	/*
	 * The function eventpoll_release() should be the first called
	 * in the file cleanup chain.
	 */
	eventpoll_release(file);
	locks_remove_flock(file);

	if (file->f_op && file->f_op->release)
		file->f_op->release(inode, file);
	security_file_free(file);
	if (unlikely(S_ISCHR(inode->i_mode) && inode->i_cdev != NULL))
		cdev_put(inode->i_cdev);
	fops_put(file->f_op);
	if (file->f_mode & FMODE_WRITE)
		put_write_access(inode);
	put_pid(file->f_owner.pid);
	file_kill(file);
	file->f_path.dentry = NULL;
	file->f_path.mnt = NULL;
	file_free(file);
	dput(dentry);
	mntput(mnt);
}

struct file *fget(unsigned int fd)
{
	struct file *file;
	struct files_struct *files = current->files;

	rcu_read_lock();
	file = fcheck_files(files, fd);
	if (file) {
		if (!atomic_inc_not_zero(&file->f_count)) {
			/* File object ref couldn't be taken */
			rcu_read_unlock();
			return NULL;
		}
	}
	rcu_read_unlock();

	return file;
}

EXPORT_SYMBOL(fget);

/*
 * Lightweight file lookup - no refcnt increment if fd table isn't shared. 
 * You can use this only if it is guranteed that the current task already 
 * holds a refcnt to that file. That check has to be done at fget() only
 * and a flag is returned to be passed to the corresponding fput_light().
 * There must not be a cloning between an fget_light/fput_light pair.
 */
struct file *fget_light(unsigned int fd, int *fput_needed)
{
	struct file *file;
	struct files_struct *files = current->files;

	*fput_needed = 0;
	if (likely((atomic_read(&files->count) == 1))) {
		file = fcheck_files(files, fd);
	} else {
		rcu_read_lock();
		file = fcheck_files(files, fd);
		if (file) {
			if (atomic_inc_not_zero(&file->f_count))
				*fput_needed = 1;
			else
				/* Didn't get the reference, someone's freed */
				file = NULL;
		}
		rcu_read_unlock();
	}

	return file;
}


void put_filp(struct file *file)
{
	if (atomic_dec_and_test(&file->f_count)) {
		security_file_free(file);
		file_kill(file);
		file_free(file);
	}
}

void file_move(struct file *file, struct list_head *list)
{
	if (!list)
		return;
	file_list_lock();
	list_move(&file->f_u.fu_list, list);
	file_list_unlock();
}

void file_kill(struct file *file)
{
	if (!list_empty(&file->f_u.fu_list)) {
		file_list_lock();
		list_del_init(&file->f_u.fu_list);
		file_list_unlock();
	}
}

int fs_may_remount_ro(struct super_block *sb)
{
	struct file *file;

	/* Check that no files are currently opened for writing. */
	file_list_lock();
	list_for_each_entry(file, &sb->s_files, f_u.fu_list) {
		struct inode *inode = file->f_path.dentry->d_inode;

		/* File with pending delete? */
		if (inode->i_nlink == 0)
			goto too_bad;

		/* Writeable file? */
		if (S_ISREG(inode->i_mode) && (file->f_mode & FMODE_WRITE))
			goto too_bad;
	}
	file_list_unlock();
	return 1; /* Tis' cool bro. */
too_bad:
	file_list_unlock();
	return 0;
}

void __init files_init(unsigned long mempages)
{ 
	int n; 
	/* One file with associated inode and dcache is very roughly 1K. 
	 * Per default don't use more than 10% of our memory for files. 
	 */ 

	n = (mempages * (PAGE_SIZE / 1024)) / 10;
	files_stat.max_files = n; 
	if (files_stat.max_files < NR_FILE)
		files_stat.max_files = NR_FILE;
	files_defer_init();
	percpu_counter_init(&nr_files, 0);
} 
