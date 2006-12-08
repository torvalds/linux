/*
 *  linux/fs/file.c
 *
 *  Copyright (C) 1998-1999, Stephen Tweedie and Bill Hawes
 *
 *  Manage the dynamic fd arrays in the process files_struct.
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/file.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>

struct fdtable_defer {
	spinlock_t lock;
	struct work_struct wq;
	struct fdtable *next;
};

/*
 * We use this list to defer free fdtables that have vmalloced
 * sets/arrays. By keeping a per-cpu list, we avoid having to embed
 * the work_struct in fdtable itself which avoids a 64 byte (i386) increase in
 * this per-task structure.
 */
static DEFINE_PER_CPU(struct fdtable_defer, fdtable_defer_list);


/*
 * Allocate an fd array, using kmalloc or vmalloc.
 * Note: the array isn't cleared at allocation time.
 */
struct file ** alloc_fd_array(int num)
{
	struct file **new_fds;
	int size = num * sizeof(struct file *);

	if (size <= PAGE_SIZE)
		new_fds = (struct file **) kmalloc(size, GFP_KERNEL);
	else 
		new_fds = (struct file **) vmalloc(size);
	return new_fds;
}

void free_fd_array(struct file **array, int num)
{
	int size = num * sizeof(struct file *);

	if (!array) {
		printk (KERN_ERR "free_fd_array: array = 0 (num = %d)\n", num);
		return;
	}

	if (num <= NR_OPEN_DEFAULT) /* Don't free the embedded fd array! */
		return;
	else if (size <= PAGE_SIZE)
		kfree(array);
	else
		vfree(array);
}

static void __free_fdtable(struct fdtable *fdt)
{
	free_fdset(fdt->open_fds, fdt->max_fdset);
	free_fdset(fdt->close_on_exec, fdt->max_fdset);
	free_fd_array(fdt->fd, fdt->max_fds);
	kfree(fdt);
}

static void free_fdtable_work(struct work_struct *work)
{
	struct fdtable_defer *f =
		container_of(work, struct fdtable_defer, wq);
	struct fdtable *fdt;

	spin_lock_bh(&f->lock);
	fdt = f->next;
	f->next = NULL;
	spin_unlock_bh(&f->lock);
	while(fdt) {
		struct fdtable *next = fdt->next;
		__free_fdtable(fdt);
		fdt = next;
	}
}

static void free_fdtable_rcu(struct rcu_head *rcu)
{
	struct fdtable *fdt = container_of(rcu, struct fdtable, rcu);
	int fdset_size, fdarray_size;
	struct fdtable_defer *fddef;

	BUG_ON(!fdt);
	fdset_size = fdt->max_fdset / 8;
	fdarray_size = fdt->max_fds * sizeof(struct file *);

	if (fdt->free_files) {
		/*
		 * The this fdtable was embedded in the files structure
		 * and the files structure itself was getting destroyed.
		 * It is now safe to free the files structure.
		 */
		kmem_cache_free(files_cachep, fdt->free_files);
		return;
	}
	if (fdt->max_fdset <= EMBEDDED_FD_SET_SIZE &&
		fdt->max_fds <= NR_OPEN_DEFAULT) {
		/*
		 * The fdtable was embedded
		 */
		return;
	}
	if (fdset_size <= PAGE_SIZE && fdarray_size <= PAGE_SIZE) {
		kfree(fdt->open_fds);
		kfree(fdt->close_on_exec);
		kfree(fdt->fd);
		kfree(fdt);
	} else {
		fddef = &get_cpu_var(fdtable_defer_list);
		spin_lock(&fddef->lock);
		fdt->next = fddef->next;
		fddef->next = fdt;
		/* vmallocs are handled from the workqueue context */
		schedule_work(&fddef->wq);
		spin_unlock(&fddef->lock);
		put_cpu_var(fdtable_defer_list);
	}
}

void free_fdtable(struct fdtable *fdt)
{
	if (fdt->free_files ||
		fdt->max_fdset > EMBEDDED_FD_SET_SIZE ||
		fdt->max_fds > NR_OPEN_DEFAULT)
		call_rcu(&fdt->rcu, free_fdtable_rcu);
}

/*
 * Expand the fdset in the files_struct.  Called with the files spinlock
 * held for write.
 */
static void copy_fdtable(struct fdtable *nfdt, struct fdtable *fdt)
{
	int i;
	int count;

	BUG_ON(nfdt->max_fdset < fdt->max_fdset);
	BUG_ON(nfdt->max_fds < fdt->max_fds);
	/* Copy the existing tables and install the new pointers */

	i = fdt->max_fdset / (sizeof(unsigned long) * 8);
	count = (nfdt->max_fdset - fdt->max_fdset) / 8;

	/*
	 * Don't copy the entire array if the current fdset is
	 * not yet initialised.
	 */
	if (i) {
		memcpy (nfdt->open_fds, fdt->open_fds,
						fdt->max_fdset/8);
		memcpy (nfdt->close_on_exec, fdt->close_on_exec,
						fdt->max_fdset/8);
		memset (&nfdt->open_fds->fds_bits[i], 0, count);
		memset (&nfdt->close_on_exec->fds_bits[i], 0, count);
	}

	/* Don't copy/clear the array if we are creating a new
	   fd array for fork() */
	if (fdt->max_fds) {
		memcpy(nfdt->fd, fdt->fd,
			fdt->max_fds * sizeof(struct file *));
		/* clear the remainder of the array */
		memset(&nfdt->fd[fdt->max_fds], 0,
		       (nfdt->max_fds - fdt->max_fds) *
					sizeof(struct file *));
	}
}

/*
 * Allocate an fdset array, using kmalloc or vmalloc.
 * Note: the array isn't cleared at allocation time.
 */
fd_set * alloc_fdset(int num)
{
	fd_set *new_fdset;
	int size = num / 8;

	if (size <= PAGE_SIZE)
		new_fdset = (fd_set *) kmalloc(size, GFP_KERNEL);
	else
		new_fdset = (fd_set *) vmalloc(size);
	return new_fdset;
}

void free_fdset(fd_set *array, int num)
{
	if (num <= EMBEDDED_FD_SET_SIZE) /* Don't free an embedded fdset */
		return;
	else if (num <= 8 * PAGE_SIZE)
		kfree(array);
	else
		vfree(array);
}

static struct fdtable *alloc_fdtable(int nr)
{
	struct fdtable *fdt = NULL;
	int nfds = 0;
  	fd_set *new_openset = NULL, *new_execset = NULL;
	struct file **new_fds;

	fdt = kzalloc(sizeof(*fdt), GFP_KERNEL);
	if (!fdt)
  		goto out;

	nfds = max_t(int, 8 * L1_CACHE_BYTES, roundup_pow_of_two(nr + 1));
	if (nfds > NR_OPEN)
		nfds = NR_OPEN;

  	new_openset = alloc_fdset(nfds);
  	new_execset = alloc_fdset(nfds);
  	if (!new_openset || !new_execset)
  		goto out;
	fdt->open_fds = new_openset;
	fdt->close_on_exec = new_execset;
	fdt->max_fdset = nfds;

	nfds = NR_OPEN_DEFAULT;
	/*
	 * Expand to the max in easy steps, and keep expanding it until
	 * we have enough for the requested fd array size.
	 */
	do {
#if NR_OPEN_DEFAULT < 256
		if (nfds < 256)
			nfds = 256;
		else
#endif
		if (nfds < (PAGE_SIZE / sizeof(struct file *)))
			nfds = PAGE_SIZE / sizeof(struct file *);
		else {
			nfds = nfds * 2;
			if (nfds > NR_OPEN)
				nfds = NR_OPEN;
  		}
	} while (nfds <= nr);
	new_fds = alloc_fd_array(nfds);
	if (!new_fds)
		goto out2;
	fdt->fd = new_fds;
	fdt->max_fds = nfds;
	fdt->free_files = NULL;
	return fdt;
out2:
	nfds = fdt->max_fdset;
out:
	free_fdset(new_openset, nfds);
	free_fdset(new_execset, nfds);
	kfree(fdt);
	return NULL;
}

/*
 * Expand the file descriptor table.
 * This function will allocate a new fdtable and both fd array and fdset, of
 * the given size.
 * Return <0 error code on error; 1 on successful completion.
 * The files->file_lock should be held on entry, and will be held on exit.
 */
static int expand_fdtable(struct files_struct *files, int nr)
	__releases(files->file_lock)
	__acquires(files->file_lock)
{
	struct fdtable *new_fdt, *cur_fdt;

	spin_unlock(&files->file_lock);
	new_fdt = alloc_fdtable(nr);
	spin_lock(&files->file_lock);
	if (!new_fdt)
		return -ENOMEM;
	/*
	 * Check again since another task may have expanded the fd table while
	 * we dropped the lock
	 */
	cur_fdt = files_fdtable(files);
	if (nr >= cur_fdt->max_fds || nr >= cur_fdt->max_fdset) {
		/* Continue as planned */
		copy_fdtable(new_fdt, cur_fdt);
		rcu_assign_pointer(files->fdt, new_fdt);
		free_fdtable(cur_fdt);
	} else {
		/* Somebody else expanded, so undo our attempt */
		__free_fdtable(new_fdt);
	}
	return 1;
}

/*
 * Expand files.
 * This function will expand the file structures, if the requested size exceeds
 * the current capacity and there is room for expansion.
 * Return <0 error code on error; 0 when nothing done; 1 when files were
 * expanded and execution may have blocked.
 * The files->file_lock should be held on entry, and will be held on exit.
 */
int expand_files(struct files_struct *files, int nr)
{
	struct fdtable *fdt;

	fdt = files_fdtable(files);
	/* Do we need to expand? */
	if (nr < fdt->max_fdset && nr < fdt->max_fds)
		return 0;
	/* Can we expand? */
	if (fdt->max_fdset >= NR_OPEN || fdt->max_fds >= NR_OPEN ||
	    nr >= NR_OPEN)
		return -EMFILE;

	/* All good, so we try */
	return expand_fdtable(files, nr);
}

static void __devinit fdtable_defer_list_init(int cpu)
{
	struct fdtable_defer *fddef = &per_cpu(fdtable_defer_list, cpu);
	spin_lock_init(&fddef->lock);
	INIT_WORK(&fddef->wq, free_fdtable_work);
	fddef->next = NULL;
}

void __init files_defer_init(void)
{
	int i;
	for_each_possible_cpu(i)
		fdtable_defer_list_init(i);
}
