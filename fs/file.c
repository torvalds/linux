// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/file.c
 *
 *  Copyright (C) 1998-1999, Stephen Tweedie and Bill Hawes
 *
 *  Manage the dynamic fd arrays in the process files_struct.
 */

#include <linux/syscalls.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/close_range.h>
#include <linux/file_ref.h>
#include <net/sock.h>
#include <linux/init_task.h>

#include "internal.h"

/**
 * __file_ref_put - Slowpath of file_ref_put()
 * @ref:	Pointer to the reference count
 * @cnt:	Current reference count
 *
 * Invoked when the reference count is outside of the valid zone.
 *
 * Return:
 *	True if this was the last reference with no future references
 *	possible. This signals the caller that it can safely schedule the
 *	object, which is protected by the reference counter, for
 *	deconstruction.
 *
 *	False if there are still active references or the put() raced
 *	with a concurrent get()/put() pair. Caller is not allowed to
 *	deconstruct the protected object.
 */
bool __file_ref_put(file_ref_t *ref, unsigned long cnt)
{
	/* Did this drop the last reference? */
	if (likely(cnt == FILE_REF_NOREF)) {
		/*
		 * Carefully try to set the reference count to FILE_REF_DEAD.
		 *
		 * This can fail if a concurrent get() operation has
		 * elevated it again or the corresponding put() even marked
		 * it dead already. Both are valid situations and do not
		 * require a retry. If this fails the caller is not
		 * allowed to deconstruct the object.
		 */
		if (!atomic_long_try_cmpxchg_release(&ref->refcnt, &cnt, FILE_REF_DEAD))
			return false;

		/*
		 * The caller can safely schedule the object for
		 * deconstruction. Provide acquire ordering.
		 */
		smp_acquire__after_ctrl_dep();
		return true;
	}

	/*
	 * If the reference count was already in the dead zone, then this
	 * put() operation is imbalanced. Warn, put the reference count back to
	 * DEAD and tell the caller to not deconstruct the object.
	 */
	if (WARN_ONCE(cnt >= FILE_REF_RELEASED, "imbalanced put on file reference count")) {
		atomic_long_set(&ref->refcnt, FILE_REF_DEAD);
		return false;
	}

	/*
	 * This is a put() operation on a saturated refcount. Restore the
	 * mean saturation value and tell the caller to not deconstruct the
	 * object.
	 */
	if (cnt > FILE_REF_MAXREF)
		atomic_long_set(&ref->refcnt, FILE_REF_SATURATED);
	return false;
}
EXPORT_SYMBOL_GPL(__file_ref_put);

unsigned int sysctl_nr_open __read_mostly = 1024*1024;
unsigned int sysctl_nr_open_min = BITS_PER_LONG;
/* our min() is unusable in constant expressions ;-/ */
#define __const_min(x, y) ((x) < (y) ? (x) : (y))
unsigned int sysctl_nr_open_max =
	__const_min(INT_MAX, ~(size_t)0/sizeof(void *)) & -BITS_PER_LONG;

static void __free_fdtable(struct fdtable *fdt)
{
	kvfree(fdt->fd);
	kvfree(fdt->open_fds);
	kfree(fdt);
}

static void free_fdtable_rcu(struct rcu_head *rcu)
{
	__free_fdtable(container_of(rcu, struct fdtable, rcu));
}

#define BITBIT_NR(nr)	BITS_TO_LONGS(BITS_TO_LONGS(nr))
#define BITBIT_SIZE(nr)	(BITBIT_NR(nr) * sizeof(long))

#define fdt_words(fdt) ((fdt)->max_fds / BITS_PER_LONG) // words in ->open_fds
/*
 * Copy 'count' fd bits from the old table to the new table and clear the extra
 * space if any.  This does not copy the file pointers.  Called with the files
 * spinlock held for write.
 */
static inline void copy_fd_bitmaps(struct fdtable *nfdt, struct fdtable *ofdt,
			    unsigned int copy_words)
{
	unsigned int nwords = fdt_words(nfdt);

	bitmap_copy_and_extend(nfdt->open_fds, ofdt->open_fds,
			copy_words * BITS_PER_LONG, nwords * BITS_PER_LONG);
	bitmap_copy_and_extend(nfdt->close_on_exec, ofdt->close_on_exec,
			copy_words * BITS_PER_LONG, nwords * BITS_PER_LONG);
	bitmap_copy_and_extend(nfdt->full_fds_bits, ofdt->full_fds_bits,
			copy_words, nwords);
}

/*
 * Copy all file descriptors from the old table to the new, expanded table and
 * clear the extra space.  Called with the files spinlock held for write.
 */
static void copy_fdtable(struct fdtable *nfdt, struct fdtable *ofdt)
{
	size_t cpy, set;

	BUG_ON(nfdt->max_fds < ofdt->max_fds);

	cpy = ofdt->max_fds * sizeof(struct file *);
	set = (nfdt->max_fds - ofdt->max_fds) * sizeof(struct file *);
	memcpy(nfdt->fd, ofdt->fd, cpy);
	memset((char *)nfdt->fd + cpy, 0, set);

	copy_fd_bitmaps(nfdt, ofdt, fdt_words(ofdt));
}

/*
 * Note how the fdtable bitmap allocations very much have to be a multiple of
 * BITS_PER_LONG. This is not only because we walk those things in chunks of
 * 'unsigned long' in some places, but simply because that is how the Linux
 * kernel bitmaps are defined to work: they are not "bits in an array of bytes",
 * they are very much "bits in an array of unsigned long".
 */
static struct fdtable *alloc_fdtable(unsigned int slots_wanted)
{
	struct fdtable *fdt;
	unsigned int nr;
	void *data;

	/*
	 * Figure out how many fds we actually want to support in this fdtable.
	 * Allocation steps are keyed to the size of the fdarray, since it
	 * grows far faster than any of the other dynamic data. We try to fit
	 * the fdarray into comfortable page-tuned chunks: starting at 1024B
	 * and growing in powers of two from there on.  Since we called only
	 * with slots_wanted > BITS_PER_LONG (embedded instance in files->fdtab
	 * already gives BITS_PER_LONG slots), the above boils down to
	 * 1.  use the smallest power of two large enough to give us that many
	 * slots.
	 * 2.  on 32bit skip 64 and 128 - the minimal capacity we want there is
	 * 256 slots (i.e. 1Kb fd array).
	 * 3.  on 64bit don't skip anything, 1Kb fd array means 128 slots there
	 * and we are never going to be asked for 64 or less.
	 */
	if (IS_ENABLED(CONFIG_32BIT) && slots_wanted < 256)
		nr = 256;
	else
		nr = roundup_pow_of_two(slots_wanted);
	/*
	 * Note that this can drive nr *below* what we had passed if sysctl_nr_open
	 * had been set lower between the check in expand_files() and here.
	 *
	 * We make sure that nr remains a multiple of BITS_PER_LONG - otherwise
	 * bitmaps handling below becomes unpleasant, to put it mildly...
	 */
	if (unlikely(nr > sysctl_nr_open)) {
		nr = round_down(sysctl_nr_open, BITS_PER_LONG);
		if (nr < slots_wanted)
			return ERR_PTR(-EMFILE);
	}

	fdt = kmalloc(sizeof(struct fdtable), GFP_KERNEL_ACCOUNT);
	if (!fdt)
		goto out;
	fdt->max_fds = nr;
	data = kvmalloc_array(nr, sizeof(struct file *), GFP_KERNEL_ACCOUNT);
	if (!data)
		goto out_fdt;
	fdt->fd = data;

	data = kvmalloc(max_t(size_t,
				 2 * nr / BITS_PER_BYTE + BITBIT_SIZE(nr), L1_CACHE_BYTES),
				 GFP_KERNEL_ACCOUNT);
	if (!data)
		goto out_arr;
	fdt->open_fds = data;
	data += nr / BITS_PER_BYTE;
	fdt->close_on_exec = data;
	data += nr / BITS_PER_BYTE;
	fdt->full_fds_bits = data;

	return fdt;

out_arr:
	kvfree(fdt->fd);
out_fdt:
	kfree(fdt);
out:
	return ERR_PTR(-ENOMEM);
}

/*
 * Expand the file descriptor table.
 * This function will allocate a new fdtable and both fd array and fdset, of
 * the given size.
 * Return <0 error code on error; 0 on successful completion.
 * The files->file_lock should be held on entry, and will be held on exit.
 */
static int expand_fdtable(struct files_struct *files, unsigned int nr)
	__releases(files->file_lock)
	__acquires(files->file_lock)
{
	struct fdtable *new_fdt, *cur_fdt;

	spin_unlock(&files->file_lock);
	new_fdt = alloc_fdtable(nr + 1);

	/* make sure all fd_install() have seen resize_in_progress
	 * or have finished their rcu_read_lock_sched() section.
	 */
	if (atomic_read(&files->count) > 1)
		synchronize_rcu();

	spin_lock(&files->file_lock);
	if (IS_ERR(new_fdt))
		return PTR_ERR(new_fdt);
	cur_fdt = files_fdtable(files);
	BUG_ON(nr < cur_fdt->max_fds);
	copy_fdtable(new_fdt, cur_fdt);
	rcu_assign_pointer(files->fdt, new_fdt);
	if (cur_fdt != &files->fdtab)
		call_rcu(&cur_fdt->rcu, free_fdtable_rcu);
	/* coupled with smp_rmb() in fd_install() */
	smp_wmb();
	return 0;
}

/*
 * Expand files.
 * This function will expand the file structures, if the requested size exceeds
 * the current capacity and there is room for expansion.
 * Return <0 error code on error; 0 on success.
 * The files->file_lock should be held on entry, and will be held on exit.
 */
static int expand_files(struct files_struct *files, unsigned int nr)
	__releases(files->file_lock)
	__acquires(files->file_lock)
{
	struct fdtable *fdt;
	int error;

repeat:
	fdt = files_fdtable(files);

	/* Do we need to expand? */
	if (nr < fdt->max_fds)
		return 0;

	/* Can we expand? */
	if (nr >= sysctl_nr_open)
		return -EMFILE;

	if (unlikely(files->resize_in_progress)) {
		spin_unlock(&files->file_lock);
		wait_event(files->resize_wait, !files->resize_in_progress);
		spin_lock(&files->file_lock);
		goto repeat;
	}

	/* All good, so we try */
	files->resize_in_progress = true;
	error = expand_fdtable(files, nr);
	files->resize_in_progress = false;

	wake_up_all(&files->resize_wait);
	return error;
}

static inline void __set_close_on_exec(unsigned int fd, struct fdtable *fdt,
				       bool set)
{
	if (set) {
		__set_bit(fd, fdt->close_on_exec);
	} else {
		if (test_bit(fd, fdt->close_on_exec))
			__clear_bit(fd, fdt->close_on_exec);
	}
}

static inline void __set_open_fd(unsigned int fd, struct fdtable *fdt, bool set)
{
	__set_bit(fd, fdt->open_fds);
	__set_close_on_exec(fd, fdt, set);
	fd /= BITS_PER_LONG;
	if (!~fdt->open_fds[fd])
		__set_bit(fd, fdt->full_fds_bits);
}

static inline void __clear_open_fd(unsigned int fd, struct fdtable *fdt)
{
	__clear_bit(fd, fdt->open_fds);
	fd /= BITS_PER_LONG;
	if (test_bit(fd, fdt->full_fds_bits))
		__clear_bit(fd, fdt->full_fds_bits);
}

static inline bool fd_is_open(unsigned int fd, const struct fdtable *fdt)
{
	return test_bit(fd, fdt->open_fds);
}

/*
 * Note that a sane fdtable size always has to be a multiple of
 * BITS_PER_LONG, since we have bitmaps that are sized by this.
 *
 * punch_hole is optional - when close_range() is asked to unshare
 * and close, we don't need to copy descriptors in that range, so
 * a smaller cloned descriptor table might suffice if the last
 * currently opened descriptor falls into that range.
 */
static unsigned int sane_fdtable_size(struct fdtable *fdt, struct fd_range *punch_hole)
{
	unsigned int last = find_last_bit(fdt->open_fds, fdt->max_fds);

	if (last == fdt->max_fds)
		return NR_OPEN_DEFAULT;
	if (punch_hole && punch_hole->to >= last && punch_hole->from <= last) {
		last = find_last_bit(fdt->open_fds, punch_hole->from);
		if (last == punch_hole->from)
			return NR_OPEN_DEFAULT;
	}
	return ALIGN(last + 1, BITS_PER_LONG);
}

/*
 * Allocate a new descriptor table and copy contents from the passed in
 * instance.  Returns a pointer to cloned table on success, ERR_PTR()
 * on failure.  For 'punch_hole' see sane_fdtable_size().
 */
struct files_struct *dup_fd(struct files_struct *oldf, struct fd_range *punch_hole)
{
	struct files_struct *newf;
	struct file **old_fds, **new_fds;
	unsigned int open_files, i;
	struct fdtable *old_fdt, *new_fdt;

	newf = kmem_cache_alloc(files_cachep, GFP_KERNEL);
	if (!newf)
		return ERR_PTR(-ENOMEM);

	atomic_set(&newf->count, 1);

	spin_lock_init(&newf->file_lock);
	newf->resize_in_progress = false;
	init_waitqueue_head(&newf->resize_wait);
	newf->next_fd = 0;
	new_fdt = &newf->fdtab;
	new_fdt->max_fds = NR_OPEN_DEFAULT;
	new_fdt->close_on_exec = newf->close_on_exec_init;
	new_fdt->open_fds = newf->open_fds_init;
	new_fdt->full_fds_bits = newf->full_fds_bits_init;
	new_fdt->fd = &newf->fd_array[0];

	spin_lock(&oldf->file_lock);
	old_fdt = files_fdtable(oldf);
	open_files = sane_fdtable_size(old_fdt, punch_hole);

	/*
	 * Check whether we need to allocate a larger fd array and fd set.
	 */
	while (unlikely(open_files > new_fdt->max_fds)) {
		spin_unlock(&oldf->file_lock);

		if (new_fdt != &newf->fdtab)
			__free_fdtable(new_fdt);

		new_fdt = alloc_fdtable(open_files);
		if (IS_ERR(new_fdt)) {
			kmem_cache_free(files_cachep, newf);
			return ERR_CAST(new_fdt);
		}

		/*
		 * Reacquire the oldf lock and a pointer to its fd table
		 * who knows it may have a new bigger fd table. We need
		 * the latest pointer.
		 */
		spin_lock(&oldf->file_lock);
		old_fdt = files_fdtable(oldf);
		open_files = sane_fdtable_size(old_fdt, punch_hole);
	}

	copy_fd_bitmaps(new_fdt, old_fdt, open_files / BITS_PER_LONG);

	old_fds = old_fdt->fd;
	new_fds = new_fdt->fd;

	for (i = open_files; i != 0; i--) {
		struct file *f = *old_fds++;
		if (f) {
			get_file(f);
		} else {
			/*
			 * The fd may be claimed in the fd bitmap but not yet
			 * instantiated in the files array if a sibling thread
			 * is partway through open().  So make sure that this
			 * fd is available to the new process.
			 */
			__clear_open_fd(open_files - i, new_fdt);
		}
		rcu_assign_pointer(*new_fds++, f);
	}
	spin_unlock(&oldf->file_lock);

	/* clear the remainder */
	memset(new_fds, 0, (new_fdt->max_fds - open_files) * sizeof(struct file *));

	rcu_assign_pointer(newf->fdt, new_fdt);

	return newf;
}

static struct fdtable *close_files(struct files_struct * files)
{
	/*
	 * It is safe to dereference the fd table without RCU or
	 * ->file_lock because this is the last reference to the
	 * files structure.
	 */
	struct fdtable *fdt = rcu_dereference_raw(files->fdt);
	unsigned int i, j = 0;

	for (;;) {
		unsigned long set;
		i = j * BITS_PER_LONG;
		if (i >= fdt->max_fds)
			break;
		set = fdt->open_fds[j++];
		while (set) {
			if (set & 1) {
				struct file *file = fdt->fd[i];
				if (file) {
					filp_close(file, files);
					cond_resched();
				}
			}
			i++;
			set >>= 1;
		}
	}

	return fdt;
}

void put_files_struct(struct files_struct *files)
{
	if (atomic_dec_and_test(&files->count)) {
		struct fdtable *fdt = close_files(files);

		/* free the arrays if they are not embedded */
		if (fdt != &files->fdtab)
			__free_fdtable(fdt);
		kmem_cache_free(files_cachep, files);
	}
}

void exit_files(struct task_struct *tsk)
{
	struct files_struct * files = tsk->files;

	if (files) {
		task_lock(tsk);
		tsk->files = NULL;
		task_unlock(tsk);
		put_files_struct(files);
	}
}

struct files_struct init_files = {
	.count		= ATOMIC_INIT(1),
	.fdt		= &init_files.fdtab,
	.fdtab		= {
		.max_fds	= NR_OPEN_DEFAULT,
		.fd		= &init_files.fd_array[0],
		.close_on_exec	= init_files.close_on_exec_init,
		.open_fds	= init_files.open_fds_init,
		.full_fds_bits	= init_files.full_fds_bits_init,
	},
	.file_lock	= __SPIN_LOCK_UNLOCKED(init_files.file_lock),
	.resize_wait	= __WAIT_QUEUE_HEAD_INITIALIZER(init_files.resize_wait),
};

static unsigned int find_next_fd(struct fdtable *fdt, unsigned int start)
{
	unsigned int maxfd = fdt->max_fds; /* always multiple of BITS_PER_LONG */
	unsigned int maxbit = maxfd / BITS_PER_LONG;
	unsigned int bitbit = start / BITS_PER_LONG;
	unsigned int bit;

	/*
	 * Try to avoid looking at the second level bitmap
	 */
	bit = find_next_zero_bit(&fdt->open_fds[bitbit], BITS_PER_LONG,
				 start & (BITS_PER_LONG - 1));
	if (bit < BITS_PER_LONG)
		return bit + bitbit * BITS_PER_LONG;

	bitbit = find_next_zero_bit(fdt->full_fds_bits, maxbit, bitbit) * BITS_PER_LONG;
	if (bitbit >= maxfd)
		return maxfd;
	if (bitbit > start)
		start = bitbit;
	return find_next_zero_bit(fdt->open_fds, maxfd, start);
}

/*
 * allocate a file descriptor, mark it busy.
 */
static int alloc_fd(unsigned start, unsigned end, unsigned flags)
{
	struct files_struct *files = current->files;
	unsigned int fd;
	int error;
	struct fdtable *fdt;

	spin_lock(&files->file_lock);
repeat:
	fdt = files_fdtable(files);
	fd = start;
	if (fd < files->next_fd)
		fd = files->next_fd;

	if (likely(fd < fdt->max_fds))
		fd = find_next_fd(fdt, fd);

	/*
	 * N.B. For clone tasks sharing a files structure, this test
	 * will limit the total number of files that can be opened.
	 */
	error = -EMFILE;
	if (unlikely(fd >= end))
		goto out;

	if (unlikely(fd >= fdt->max_fds)) {
		error = expand_files(files, fd);
		if (error < 0)
			goto out;

		goto repeat;
	}

	if (start <= files->next_fd)
		files->next_fd = fd + 1;

	__set_open_fd(fd, fdt, flags & O_CLOEXEC);
	error = fd;

out:
	spin_unlock(&files->file_lock);
	return error;
}

int __get_unused_fd_flags(unsigned flags, unsigned long nofile)
{
	return alloc_fd(0, nofile, flags);
}

int get_unused_fd_flags(unsigned flags)
{
	return __get_unused_fd_flags(flags, rlimit(RLIMIT_NOFILE));
}
EXPORT_SYMBOL(get_unused_fd_flags);

static void __put_unused_fd(struct files_struct *files, unsigned int fd)
{
	struct fdtable *fdt = files_fdtable(files);
	__clear_open_fd(fd, fdt);
	if (fd < files->next_fd)
		files->next_fd = fd;
}

void put_unused_fd(unsigned int fd)
{
	struct files_struct *files = current->files;
	spin_lock(&files->file_lock);
	__put_unused_fd(files, fd);
	spin_unlock(&files->file_lock);
}

EXPORT_SYMBOL(put_unused_fd);

/*
 * Install a file pointer in the fd array.
 *
 * The VFS is full of places where we drop the files lock between
 * setting the open_fds bitmap and installing the file in the file
 * array.  At any such point, we are vulnerable to a dup2() race
 * installing a file in the array before us.  We need to detect this and
 * fput() the struct file we are about to overwrite in this case.
 *
 * It should never happen - if we allow dup2() do it, _really_ bad things
 * will follow.
 *
 * This consumes the "file" refcount, so callers should treat it
 * as if they had called fput(file).
 */

void fd_install(unsigned int fd, struct file *file)
{
	struct files_struct *files = current->files;
	struct fdtable *fdt;

	if (WARN_ON_ONCE(unlikely(file->f_mode & FMODE_BACKING)))
		return;

	rcu_read_lock_sched();

	if (unlikely(files->resize_in_progress)) {
		rcu_read_unlock_sched();
		spin_lock(&files->file_lock);
		fdt = files_fdtable(files);
		WARN_ON(fdt->fd[fd] != NULL);
		rcu_assign_pointer(fdt->fd[fd], file);
		spin_unlock(&files->file_lock);
		return;
	}
	/* coupled with smp_wmb() in expand_fdtable() */
	smp_rmb();
	fdt = rcu_dereference_sched(files->fdt);
	BUG_ON(fdt->fd[fd] != NULL);
	rcu_assign_pointer(fdt->fd[fd], file);
	rcu_read_unlock_sched();
}

EXPORT_SYMBOL(fd_install);

/**
 * file_close_fd_locked - return file associated with fd
 * @files: file struct to retrieve file from
 * @fd: file descriptor to retrieve file for
 *
 * Doesn't take a separate reference count.
 *
 * Context: files_lock must be held.
 *
 * Returns: The file associated with @fd (NULL if @fd is not open)
 */
struct file *file_close_fd_locked(struct files_struct *files, unsigned fd)
{
	struct fdtable *fdt = files_fdtable(files);
	struct file *file;

	lockdep_assert_held(&files->file_lock);

	if (fd >= fdt->max_fds)
		return NULL;

	fd = array_index_nospec(fd, fdt->max_fds);
	file = fdt->fd[fd];
	if (file) {
		rcu_assign_pointer(fdt->fd[fd], NULL);
		__put_unused_fd(files, fd);
	}
	return file;
}

int close_fd(unsigned fd)
{
	struct files_struct *files = current->files;
	struct file *file;

	spin_lock(&files->file_lock);
	file = file_close_fd_locked(files, fd);
	spin_unlock(&files->file_lock);
	if (!file)
		return -EBADF;

	return filp_close(file, files);
}
EXPORT_SYMBOL(close_fd);

/**
 * last_fd - return last valid index into fd table
 * @fdt: File descriptor table.
 *
 * Context: Either rcu read lock or files_lock must be held.
 *
 * Returns: Last valid index into fdtable.
 */
static inline unsigned last_fd(struct fdtable *fdt)
{
	return fdt->max_fds - 1;
}

static inline void __range_cloexec(struct files_struct *cur_fds,
				   unsigned int fd, unsigned int max_fd)
{
	struct fdtable *fdt;

	/* make sure we're using the correct maximum value */
	spin_lock(&cur_fds->file_lock);
	fdt = files_fdtable(cur_fds);
	max_fd = min(last_fd(fdt), max_fd);
	if (fd <= max_fd)
		bitmap_set(fdt->close_on_exec, fd, max_fd - fd + 1);
	spin_unlock(&cur_fds->file_lock);
}

static inline void __range_close(struct files_struct *files, unsigned int fd,
				 unsigned int max_fd)
{
	struct file *file;
	unsigned n;

	spin_lock(&files->file_lock);
	n = last_fd(files_fdtable(files));
	max_fd = min(max_fd, n);

	for (; fd <= max_fd; fd++) {
		file = file_close_fd_locked(files, fd);
		if (file) {
			spin_unlock(&files->file_lock);
			filp_close(file, files);
			cond_resched();
			spin_lock(&files->file_lock);
		} else if (need_resched()) {
			spin_unlock(&files->file_lock);
			cond_resched();
			spin_lock(&files->file_lock);
		}
	}
	spin_unlock(&files->file_lock);
}

/**
 * sys_close_range() - Close all file descriptors in a given range.
 *
 * @fd:     starting file descriptor to close
 * @max_fd: last file descriptor to close
 * @flags:  CLOSE_RANGE flags.
 *
 * This closes a range of file descriptors. All file descriptors
 * from @fd up to and including @max_fd are closed.
 * Currently, errors to close a given file descriptor are ignored.
 */
SYSCALL_DEFINE3(close_range, unsigned int, fd, unsigned int, max_fd,
		unsigned int, flags)
{
	struct task_struct *me = current;
	struct files_struct *cur_fds = me->files, *fds = NULL;

	if (flags & ~(CLOSE_RANGE_UNSHARE | CLOSE_RANGE_CLOEXEC))
		return -EINVAL;

	if (fd > max_fd)
		return -EINVAL;

	if ((flags & CLOSE_RANGE_UNSHARE) && atomic_read(&cur_fds->count) > 1) {
		struct fd_range range = {fd, max_fd}, *punch_hole = &range;

		/*
		 * If the caller requested all fds to be made cloexec we always
		 * copy all of the file descriptors since they still want to
		 * use them.
		 */
		if (flags & CLOSE_RANGE_CLOEXEC)
			punch_hole = NULL;

		fds = dup_fd(cur_fds, punch_hole);
		if (IS_ERR(fds))
			return PTR_ERR(fds);
		/*
		 * We used to share our file descriptor table, and have now
		 * created a private one, make sure we're using it below.
		 */
		swap(cur_fds, fds);
	}

	if (flags & CLOSE_RANGE_CLOEXEC)
		__range_cloexec(cur_fds, fd, max_fd);
	else
		__range_close(cur_fds, fd, max_fd);

	if (fds) {
		/*
		 * We're done closing the files we were supposed to. Time to install
		 * the new file descriptor table and drop the old one.
		 */
		task_lock(me);
		me->files = cur_fds;
		task_unlock(me);
		put_files_struct(fds);
	}

	return 0;
}

/**
 * file_close_fd - return file associated with fd
 * @fd: file descriptor to retrieve file for
 *
 * Doesn't take a separate reference count.
 *
 * Returns: The file associated with @fd (NULL if @fd is not open)
 */
struct file *file_close_fd(unsigned int fd)
{
	struct files_struct *files = current->files;
	struct file *file;

	spin_lock(&files->file_lock);
	file = file_close_fd_locked(files, fd);
	spin_unlock(&files->file_lock);

	return file;
}

void do_close_on_exec(struct files_struct *files)
{
	unsigned i;
	struct fdtable *fdt;

	/* exec unshares first */
	spin_lock(&files->file_lock);
	for (i = 0; ; i++) {
		unsigned long set;
		unsigned fd = i * BITS_PER_LONG;
		fdt = files_fdtable(files);
		if (fd >= fdt->max_fds)
			break;
		set = fdt->close_on_exec[i];
		if (!set)
			continue;
		fdt->close_on_exec[i] = 0;
		for ( ; set ; fd++, set >>= 1) {
			struct file *file;
			if (!(set & 1))
				continue;
			file = fdt->fd[fd];
			if (!file)
				continue;
			rcu_assign_pointer(fdt->fd[fd], NULL);
			__put_unused_fd(files, fd);
			spin_unlock(&files->file_lock);
			filp_close(file, files);
			cond_resched();
			spin_lock(&files->file_lock);
		}

	}
	spin_unlock(&files->file_lock);
}

static struct file *__get_file_rcu(struct file __rcu **f)
{
	struct file __rcu *file;
	struct file __rcu *file_reloaded;
	struct file __rcu *file_reloaded_cmp;

	file = rcu_dereference_raw(*f);
	if (!file)
		return NULL;

	if (unlikely(!file_ref_get(&file->f_ref)))
		return ERR_PTR(-EAGAIN);

	file_reloaded = rcu_dereference_raw(*f);

	/*
	 * Ensure that all accesses have a dependency on the load from
	 * rcu_dereference_raw() above so we get correct ordering
	 * between reuse/allocation and the pointer check below.
	 */
	file_reloaded_cmp = file_reloaded;
	OPTIMIZER_HIDE_VAR(file_reloaded_cmp);

	/*
	 * file_ref_get() above provided a full memory barrier when we
	 * acquired a reference.
	 *
	 * This is paired with the write barrier from assigning to the
	 * __rcu protected file pointer so that if that pointer still
	 * matches the current file, we know we have successfully
	 * acquired a reference to the right file.
	 *
	 * If the pointers don't match the file has been reallocated by
	 * SLAB_TYPESAFE_BY_RCU.
	 */
	if (file == file_reloaded_cmp)
		return file_reloaded;

	fput(file);
	return ERR_PTR(-EAGAIN);
}

/**
 * get_file_rcu - try go get a reference to a file under rcu
 * @f: the file to get a reference on
 *
 * This function tries to get a reference on @f carefully verifying that
 * @f hasn't been reused.
 *
 * This function should rarely have to be used and only by users who
 * understand the implications of SLAB_TYPESAFE_BY_RCU. Try to avoid it.
 *
 * Return: Returns @f with the reference count increased or NULL.
 */
struct file *get_file_rcu(struct file __rcu **f)
{
	for (;;) {
		struct file __rcu *file;

		file = __get_file_rcu(f);
		if (!IS_ERR(file))
			return file;
	}
}
EXPORT_SYMBOL_GPL(get_file_rcu);

/**
 * get_file_active - try go get a reference to a file
 * @f: the file to get a reference on
 *
 * In contast to get_file_rcu() the pointer itself isn't part of the
 * reference counting.
 *
 * This function should rarely have to be used and only by users who
 * understand the implications of SLAB_TYPESAFE_BY_RCU. Try to avoid it.
 *
 * Return: Returns @f with the reference count increased or NULL.
 */
struct file *get_file_active(struct file **f)
{
	struct file __rcu *file;

	rcu_read_lock();
	file = __get_file_rcu(f);
	rcu_read_unlock();
	if (IS_ERR(file))
		file = NULL;
	return file;
}
EXPORT_SYMBOL_GPL(get_file_active);

static inline struct file *__fget_files_rcu(struct files_struct *files,
       unsigned int fd, fmode_t mask)
{
	for (;;) {
		struct file *file;
		struct fdtable *fdt = rcu_dereference_raw(files->fdt);
		struct file __rcu **fdentry;
		unsigned long nospec_mask;

		/* Mask is a 0 for invalid fd's, ~0 for valid ones */
		nospec_mask = array_index_mask_nospec(fd, fdt->max_fds);

		/*
		 * fdentry points to the 'fd' offset, or fdt->fd[0].
		 * Loading from fdt->fd[0] is always safe, because the
		 * array always exists.
		 */
		fdentry = fdt->fd + (fd & nospec_mask);

		/* Do the load, then mask any invalid result */
		file = rcu_dereference_raw(*fdentry);
		file = (void *)(nospec_mask & (unsigned long)file);
		if (unlikely(!file))
			return NULL;

		/*
		 * Ok, we have a file pointer that was valid at
		 * some point, but it might have become stale since.
		 *
		 * We need to confirm it by incrementing the refcount
		 * and then check the lookup again.
		 *
		 * file_ref_get() gives us a full memory barrier. We
		 * only really need an 'acquire' one to protect the
		 * loads below, but we don't have that.
		 */
		if (unlikely(!file_ref_get(&file->f_ref)))
			continue;

		/*
		 * Such a race can take two forms:
		 *
		 *  (a) the file ref already went down to zero and the
		 *      file hasn't been reused yet or the file count
		 *      isn't zero but the file has already been reused.
		 *
		 *  (b) the file table entry has changed under us.
		 *       Note that we don't need to re-check the 'fdt->fd'
		 *       pointer having changed, because it always goes
		 *       hand-in-hand with 'fdt'.
		 *
		 * If so, we need to put our ref and try again.
		 */
		if (unlikely(file != rcu_dereference_raw(*fdentry)) ||
		    unlikely(rcu_dereference_raw(files->fdt) != fdt)) {
			fput(file);
			continue;
		}

		/*
		 * This isn't the file we're looking for or we're not
		 * allowed to get a reference to it.
		 */
		if (unlikely(file->f_mode & mask)) {
			fput(file);
			return NULL;
		}

		/*
		 * Ok, we have a ref to the file, and checked that it
		 * still exists.
		 */
		return file;
	}
}

static struct file *__fget_files(struct files_struct *files, unsigned int fd,
				 fmode_t mask)
{
	struct file *file;

	rcu_read_lock();
	file = __fget_files_rcu(files, fd, mask);
	rcu_read_unlock();

	return file;
}

static inline struct file *__fget(unsigned int fd, fmode_t mask)
{
	return __fget_files(current->files, fd, mask);
}

struct file *fget(unsigned int fd)
{
	return __fget(fd, FMODE_PATH);
}
EXPORT_SYMBOL(fget);

struct file *fget_raw(unsigned int fd)
{
	return __fget(fd, 0);
}
EXPORT_SYMBOL(fget_raw);

struct file *fget_task(struct task_struct *task, unsigned int fd)
{
	struct file *file = NULL;

	task_lock(task);
	if (task->files)
		file = __fget_files(task->files, fd, 0);
	task_unlock(task);

	return file;
}

struct file *fget_task_next(struct task_struct *task, unsigned int *ret_fd)
{
	/* Must be called with rcu_read_lock held */
	struct files_struct *files;
	unsigned int fd = *ret_fd;
	struct file *file = NULL;

	task_lock(task);
	files = task->files;
	if (files) {
		rcu_read_lock();
		for (; fd < files_fdtable(files)->max_fds; fd++) {
			file = __fget_files_rcu(files, fd, 0);
			if (file)
				break;
		}
		rcu_read_unlock();
	}
	task_unlock(task);
	*ret_fd = fd;
	return file;
}
EXPORT_SYMBOL(fget_task_next);

/*
 * Lightweight file lookup - no refcnt increment if fd table isn't shared.
 *
 * You can use this instead of fget if you satisfy all of the following
 * conditions:
 * 1) You must call fput_light before exiting the syscall and returning control
 *    to userspace (i.e. you cannot remember the returned struct file * after
 *    returning to userspace).
 * 2) You must not call filp_close on the returned struct file * in between
 *    calls to fget_light and fput_light.
 * 3) You must not clone the current task in between the calls to fget_light
 *    and fput_light.
 *
 * The fput_needed flag returned by fget_light should be passed to the
 * corresponding fput_light.
 *
 * (As an exception to rule 2, you can call filp_close between fget_light and
 * fput_light provided that you capture a real refcount with get_file before
 * the call to filp_close, and ensure that this real refcount is fput *after*
 * the fput_light call.)
 *
 * See also the documentation in rust/kernel/file.rs.
 */
static inline struct fd __fget_light(unsigned int fd, fmode_t mask)
{
	struct files_struct *files = current->files;
	struct file *file;

	/*
	 * If another thread is concurrently calling close_fd() followed
	 * by put_files_struct(), we must not observe the old table
	 * entry combined with the new refcount - otherwise we could
	 * return a file that is concurrently being freed.
	 *
	 * atomic_read_acquire() pairs with atomic_dec_and_test() in
	 * put_files_struct().
	 */
	if (likely(atomic_read_acquire(&files->count) == 1)) {
		file = files_lookup_fd_raw(files, fd);
		if (!file || unlikely(file->f_mode & mask))
			return EMPTY_FD;
		return BORROWED_FD(file);
	} else {
		file = __fget_files(files, fd, mask);
		if (!file)
			return EMPTY_FD;
		return CLONED_FD(file);
	}
}
struct fd fdget(unsigned int fd)
{
	return __fget_light(fd, FMODE_PATH);
}
EXPORT_SYMBOL(fdget);

struct fd fdget_raw(unsigned int fd)
{
	return __fget_light(fd, 0);
}

/*
 * Try to avoid f_pos locking. We only need it if the
 * file is marked for FMODE_ATOMIC_POS, and it can be
 * accessed multiple ways.
 *
 * Always do it for directories, because pidfd_getfd()
 * can make a file accessible even if it otherwise would
 * not be, and for directories this is a correctness
 * issue, not a "POSIX requirement".
 */
static inline bool file_needs_f_pos_lock(struct file *file)
{
	return (file->f_mode & FMODE_ATOMIC_POS) &&
		(file_count(file) > 1 || file->f_op->iterate_shared);
}

struct fd fdget_pos(unsigned int fd)
{
	struct fd f = fdget(fd);
	struct file *file = fd_file(f);

	if (file && file_needs_f_pos_lock(file)) {
		f.word |= FDPUT_POS_UNLOCK;
		mutex_lock(&file->f_pos_lock);
	}
	return f;
}

void __f_unlock_pos(struct file *f)
{
	mutex_unlock(&f->f_pos_lock);
}

/*
 * We only lock f_pos if we have threads or if the file might be
 * shared with another process. In both cases we'll have an elevated
 * file count (done either by fdget() or by fork()).
 */

void set_close_on_exec(unsigned int fd, int flag)
{
	struct files_struct *files = current->files;
	spin_lock(&files->file_lock);
	__set_close_on_exec(fd, files_fdtable(files), flag);
	spin_unlock(&files->file_lock);
}

bool get_close_on_exec(unsigned int fd)
{
	bool res;
	rcu_read_lock();
	res = close_on_exec(fd, current->files);
	rcu_read_unlock();
	return res;
}

static int do_dup2(struct files_struct *files,
	struct file *file, unsigned fd, unsigned flags)
__releases(&files->file_lock)
{
	struct file *tofree;
	struct fdtable *fdt;

	/*
	 * We need to detect attempts to do dup2() over allocated but still
	 * not finished descriptor.  NB: OpenBSD avoids that at the price of
	 * extra work in their equivalent of fget() - they insert struct
	 * file immediately after grabbing descriptor, mark it larval if
	 * more work (e.g. actual opening) is needed and make sure that
	 * fget() treats larval files as absent.  Potentially interesting,
	 * but while extra work in fget() is trivial, locking implications
	 * and amount of surgery on open()-related paths in VFS are not.
	 * FreeBSD fails with -EBADF in the same situation, NetBSD "solution"
	 * deadlocks in rather amusing ways, AFAICS.  All of that is out of
	 * scope of POSIX or SUS, since neither considers shared descriptor
	 * tables and this condition does not arise without those.
	 */
	fdt = files_fdtable(files);
	fd = array_index_nospec(fd, fdt->max_fds);
	tofree = fdt->fd[fd];
	if (!tofree && fd_is_open(fd, fdt))
		goto Ebusy;
	get_file(file);
	rcu_assign_pointer(fdt->fd[fd], file);
	__set_open_fd(fd, fdt, flags & O_CLOEXEC);
	spin_unlock(&files->file_lock);

	if (tofree)
		filp_close(tofree, files);

	return fd;

Ebusy:
	spin_unlock(&files->file_lock);
	return -EBUSY;
}

int replace_fd(unsigned fd, struct file *file, unsigned flags)
{
	int err;
	struct files_struct *files = current->files;

	if (!file)
		return close_fd(fd);

	if (fd >= rlimit(RLIMIT_NOFILE))
		return -EBADF;

	spin_lock(&files->file_lock);
	err = expand_files(files, fd);
	if (unlikely(err < 0))
		goto out_unlock;
	return do_dup2(files, file, fd, flags);

out_unlock:
	spin_unlock(&files->file_lock);
	return err;
}

/**
 * receive_fd() - Install received file into file descriptor table
 * @file: struct file that was received from another process
 * @ufd: __user pointer to write new fd number to
 * @o_flags: the O_* flags to apply to the new fd entry
 *
 * Installs a received file into the file descriptor table, with appropriate
 * checks and count updates. Optionally writes the fd number to userspace, if
 * @ufd is non-NULL.
 *
 * This helper handles its own reference counting of the incoming
 * struct file.
 *
 * Returns newly install fd or -ve on error.
 */
int receive_fd(struct file *file, int __user *ufd, unsigned int o_flags)
{
	int new_fd;
	int error;

	error = security_file_receive(file);
	if (error)
		return error;

	new_fd = get_unused_fd_flags(o_flags);
	if (new_fd < 0)
		return new_fd;

	if (ufd) {
		error = put_user(new_fd, ufd);
		if (error) {
			put_unused_fd(new_fd);
			return error;
		}
	}

	fd_install(new_fd, get_file(file));
	__receive_sock(file);
	return new_fd;
}
EXPORT_SYMBOL_GPL(receive_fd);

int receive_fd_replace(int new_fd, struct file *file, unsigned int o_flags)
{
	int error;

	error = security_file_receive(file);
	if (error)
		return error;
	error = replace_fd(new_fd, file, o_flags);
	if (error)
		return error;
	__receive_sock(file);
	return new_fd;
}

static int ksys_dup3(unsigned int oldfd, unsigned int newfd, int flags)
{
	int err = -EBADF;
	struct file *file;
	struct files_struct *files = current->files;

	if ((flags & ~O_CLOEXEC) != 0)
		return -EINVAL;

	if (unlikely(oldfd == newfd))
		return -EINVAL;

	if (newfd >= rlimit(RLIMIT_NOFILE))
		return -EBADF;

	spin_lock(&files->file_lock);
	err = expand_files(files, newfd);
	file = files_lookup_fd_locked(files, oldfd);
	if (unlikely(!file))
		goto Ebadf;
	if (unlikely(err < 0)) {
		if (err == -EMFILE)
			goto Ebadf;
		goto out_unlock;
	}
	return do_dup2(files, file, newfd, flags);

Ebadf:
	err = -EBADF;
out_unlock:
	spin_unlock(&files->file_lock);
	return err;
}

SYSCALL_DEFINE3(dup3, unsigned int, oldfd, unsigned int, newfd, int, flags)
{
	return ksys_dup3(oldfd, newfd, flags);
}

SYSCALL_DEFINE2(dup2, unsigned int, oldfd, unsigned int, newfd)
{
	if (unlikely(newfd == oldfd)) { /* corner case */
		struct files_struct *files = current->files;
		struct file *f;
		int retval = oldfd;

		rcu_read_lock();
		f = __fget_files_rcu(files, oldfd, 0);
		if (!f)
			retval = -EBADF;
		rcu_read_unlock();
		if (f)
			fput(f);
		return retval;
	}
	return ksys_dup3(oldfd, newfd, 0);
}

SYSCALL_DEFINE1(dup, unsigned int, fildes)
{
	int ret = -EBADF;
	struct file *file = fget_raw(fildes);

	if (file) {
		ret = get_unused_fd_flags(0);
		if (ret >= 0)
			fd_install(ret, file);
		else
			fput(file);
	}
	return ret;
}

int f_dupfd(unsigned int from, struct file *file, unsigned flags)
{
	unsigned long nofile = rlimit(RLIMIT_NOFILE);
	int err;
	if (from >= nofile)
		return -EINVAL;
	err = alloc_fd(from, nofile, flags);
	if (err >= 0) {
		get_file(file);
		fd_install(err, file);
	}
	return err;
}

int iterate_fd(struct files_struct *files, unsigned n,
		int (*f)(const void *, struct file *, unsigned),
		const void *p)
{
	struct fdtable *fdt;
	int res = 0;
	if (!files)
		return 0;
	spin_lock(&files->file_lock);
	for (fdt = files_fdtable(files); n < fdt->max_fds; n++) {
		struct file *file;
		file = rcu_dereference_check_fdtable(files, fdt->fd[n]);
		if (!file)
			continue;
		res = f(p, file, n);
		if (res)
			break;
	}
	spin_unlock(&files->file_lock);
	return res;
}
EXPORT_SYMBOL(iterate_fd);
