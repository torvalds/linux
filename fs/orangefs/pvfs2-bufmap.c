/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "protocol.h"
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

DECLARE_WAIT_QUEUE_HEAD(pvfs2_bufmap_init_waitq);

struct pvfs2_bufmap {
	atomic_t refcnt;

	int desc_size;
	int desc_shift;
	int desc_count;
	int total_size;
	int page_count;

	struct page **page_array;
	struct pvfs_bufmap_desc *desc_array;

	/* array to track usage of buffer descriptors */
	int *buffer_index_array;
	spinlock_t buffer_index_lock;

	/* array to track usage of buffer descriptors for readdir */
	int readdir_index_array[PVFS2_READDIR_DEFAULT_DESC_COUNT];
	spinlock_t readdir_index_lock;
} *__pvfs2_bufmap;

static DEFINE_SPINLOCK(pvfs2_bufmap_lock);

static void
pvfs2_bufmap_unmap(struct pvfs2_bufmap *bufmap)
{
	int i;

	for (i = 0; i < bufmap->page_count; i++)
		page_cache_release(bufmap->page_array[i]);
}

static void
pvfs2_bufmap_free(struct pvfs2_bufmap *bufmap)
{
	kfree(bufmap->page_array);
	kfree(bufmap->desc_array);
	kfree(bufmap->buffer_index_array);
	kfree(bufmap);
}

struct pvfs2_bufmap *pvfs2_bufmap_ref(void)
{
	struct pvfs2_bufmap *bufmap = NULL;

	spin_lock(&pvfs2_bufmap_lock);
	if (__pvfs2_bufmap) {
		bufmap = __pvfs2_bufmap;
		atomic_inc(&bufmap->refcnt);
	}
	spin_unlock(&pvfs2_bufmap_lock);
	return bufmap;
}

void pvfs2_bufmap_unref(struct pvfs2_bufmap *bufmap)
{
	if (atomic_dec_and_lock(&bufmap->refcnt, &pvfs2_bufmap_lock)) {
		__pvfs2_bufmap = NULL;
		spin_unlock(&pvfs2_bufmap_lock);

		pvfs2_bufmap_unmap(bufmap);
		pvfs2_bufmap_free(bufmap);
	}
}

inline int pvfs_bufmap_size_query(void)
{
	struct pvfs2_bufmap *bufmap = pvfs2_bufmap_ref();
	int size = bufmap ? bufmap->desc_size : 0;

	pvfs2_bufmap_unref(bufmap);
	return size;
}

inline int pvfs_bufmap_shift_query(void)
{
	struct pvfs2_bufmap *bufmap = pvfs2_bufmap_ref();
	int shift = bufmap ? bufmap->desc_shift : 0;

	pvfs2_bufmap_unref(bufmap);
	return shift;
}

static DECLARE_WAIT_QUEUE_HEAD(bufmap_waitq);
static DECLARE_WAIT_QUEUE_HEAD(readdir_waitq);

/*
 * get_bufmap_init
 *
 * If bufmap_init is 1, then the shared memory system, including the
 * buffer_index_array, is available.  Otherwise, it is not.
 *
 * returns the value of bufmap_init
 */
int get_bufmap_init(void)
{
	return __pvfs2_bufmap ? 1 : 0;
}


static struct pvfs2_bufmap *
pvfs2_bufmap_alloc(struct PVFS_dev_map_desc *user_desc)
{
	struct pvfs2_bufmap *bufmap;

	bufmap = kzalloc(sizeof(*bufmap), GFP_KERNEL);
	if (!bufmap)
		goto out;

	atomic_set(&bufmap->refcnt, 1);
	bufmap->total_size = user_desc->total_size;
	bufmap->desc_count = user_desc->count;
	bufmap->desc_size = user_desc->size;
	bufmap->desc_shift = ilog2(bufmap->desc_size);

	spin_lock_init(&bufmap->buffer_index_lock);
	bufmap->buffer_index_array =
		kcalloc(bufmap->desc_count, sizeof(int), GFP_KERNEL);
	if (!bufmap->buffer_index_array) {
		gossip_err("pvfs2: could not allocate %d buffer indices\n",
				bufmap->desc_count);
		goto out_free_bufmap;
	}
	spin_lock_init(&bufmap->readdir_index_lock);

	bufmap->desc_array =
		kcalloc(bufmap->desc_count, sizeof(struct pvfs_bufmap_desc),
			GFP_KERNEL);
	if (!bufmap->desc_array) {
		gossip_err("pvfs2: could not allocate %d descriptors\n",
				bufmap->desc_count);
		goto out_free_index_array;
	}

	bufmap->page_count = bufmap->total_size / PAGE_SIZE;

	/* allocate storage to track our page mappings */
	bufmap->page_array =
		kcalloc(bufmap->page_count, sizeof(struct page *), GFP_KERNEL);
	if (!bufmap->page_array)
		goto out_free_desc_array;

	return bufmap;

out_free_desc_array:
	kfree(bufmap->desc_array);
out_free_index_array:
	kfree(bufmap->buffer_index_array);
out_free_bufmap:
	kfree(bufmap);
out:
	return NULL;
}

static int
pvfs2_bufmap_map(struct pvfs2_bufmap *bufmap,
		struct PVFS_dev_map_desc *user_desc)
{
	int pages_per_desc = bufmap->desc_size / PAGE_SIZE;
	int offset = 0, ret, i;

	/* map the pages */
	down_write(&current->mm->mmap_sem);
	ret = get_user_pages(current,
			     current->mm,
			     (unsigned long)user_desc->ptr,
			     bufmap->page_count,
			     1,
			     0,
			     bufmap->page_array,
			     NULL);
	up_write(&current->mm->mmap_sem);

	if (ret < 0)
		return ret;

	if (ret != bufmap->page_count) {
		gossip_err("pvfs2 error: asked for %d pages, only got %d.\n",
				bufmap->page_count, ret);

		for (i = 0; i < ret; i++) {
			SetPageError(bufmap->page_array[i]);
			page_cache_release(bufmap->page_array[i]);
		}
		return -ENOMEM;
	}

	/*
	 * ideally we want to get kernel space pointers for each page, but
	 * we can't kmap that many pages at once if highmem is being used.
	 * so instead, we just kmap/kunmap the page address each time the
	 * kaddr is needed.
	 */
	for (i = 0; i < bufmap->page_count; i++)
		flush_dcache_page(bufmap->page_array[i]);

	/* build a list of available descriptors */
	for (offset = 0, i = 0; i < bufmap->desc_count; i++) {
		bufmap->desc_array[i].page_array = &bufmap->page_array[offset];
		bufmap->desc_array[i].array_count = pages_per_desc;
		bufmap->desc_array[i].uaddr =
		    (user_desc->ptr + (i * pages_per_desc * PAGE_SIZE));
		offset += pages_per_desc;
	}

	return 0;
}

/*
 * pvfs_bufmap_initialize()
 *
 * initializes the mapped buffer interface
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_initialize(struct PVFS_dev_map_desc *user_desc)
{
	struct pvfs2_bufmap *bufmap;
	int ret = -EINVAL;

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "pvfs_bufmap_initialize: called (ptr ("
		     "%p) sz (%d) cnt(%d).\n",
		     user_desc->ptr,
		     user_desc->size,
		     user_desc->count);

	/*
	 * sanity check alignment and size of buffer that caller wants to
	 * work with
	 */
	if (PAGE_ALIGN((unsigned long)user_desc->ptr) !=
	    (unsigned long)user_desc->ptr) {
		gossip_err("pvfs2 error: memory alignment (front). %p\n",
			   user_desc->ptr);
		goto out;
	}

	if (PAGE_ALIGN(((unsigned long)user_desc->ptr + user_desc->total_size))
	    != (unsigned long)(user_desc->ptr + user_desc->total_size)) {
		gossip_err("pvfs2 error: memory alignment (back).(%p + %d)\n",
			   user_desc->ptr,
			   user_desc->total_size);
		goto out;
	}

	if (user_desc->total_size != (user_desc->size * user_desc->count)) {
		gossip_err("pvfs2 error: user provided an oddly sized buffer: (%d, %d, %d)\n",
			   user_desc->total_size,
			   user_desc->size,
			   user_desc->count);
		goto out;
	}

	if ((user_desc->size % PAGE_SIZE) != 0) {
		gossip_err("pvfs2 error: bufmap size not page size divisible (%d).\n",
			   user_desc->size);
		goto out;
	}

	ret = -ENOMEM;
	bufmap = pvfs2_bufmap_alloc(user_desc);
	if (!bufmap)
		goto out;

	ret = pvfs2_bufmap_map(bufmap, user_desc);
	if (ret)
		goto out_free_bufmap;


	spin_lock(&pvfs2_bufmap_lock);
	if (__pvfs2_bufmap) {
		spin_unlock(&pvfs2_bufmap_lock);
		gossip_err("pvfs2: error: bufmap already initialized.\n");
		ret = -EALREADY;
		goto out_unmap_bufmap;
	}
	__pvfs2_bufmap = bufmap;
	spin_unlock(&pvfs2_bufmap_lock);

	/*
	 * If there are operations in pvfs2_bufmap_init_waitq, wake them up.
	 * This scenario occurs when the client-core is restarted and I/O
	 * requests in the in-progress or waiting tables are restarted.  I/O
	 * requests cannot be restarted until the shared memory system is
	 * completely re-initialized, so we put the I/O requests in this
	 * waitq until initialization has completed.  NOTE:  the I/O requests
	 * are also on a timer, so they don't wait forever just in case the
	 * client-core doesn't come back up.
	 */
	wake_up_interruptible(&pvfs2_bufmap_init_waitq);

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "pvfs_bufmap_initialize: exiting normally\n");
	return 0;

out_unmap_bufmap:
	pvfs2_bufmap_unmap(bufmap);
out_free_bufmap:
	pvfs2_bufmap_free(bufmap);
out:
	return ret;
}

/*
 * pvfs_bufmap_finalize()
 *
 * shuts down the mapped buffer interface and releases any resources
 * associated with it
 *
 * no return value
 */
void pvfs_bufmap_finalize(void)
{
	gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2_bufmap_finalize: called\n");
	BUG_ON(!__pvfs2_bufmap);
	pvfs2_bufmap_unref(__pvfs2_bufmap);
	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "pvfs2_bufmap_finalize: exiting normally\n");
}

struct slot_args {
	int slot_count;
	int *slot_array;
	spinlock_t *slot_lock;
	wait_queue_head_t *slot_wq;
};

static int wait_for_a_slot(struct slot_args *slargs, int *buffer_index)
{
	int ret = -1;
	int i = 0;
	DECLARE_WAITQUEUE(my_wait, current);


	add_wait_queue_exclusive(slargs->slot_wq, &my_wait);

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		/*
		 * check for available desc, slot_lock is the appropriate
		 * index_lock
		 */
		spin_lock(slargs->slot_lock);
		for (i = 0; i < slargs->slot_count; i++)
			if (slargs->slot_array[i] == 0) {
				slargs->slot_array[i] = 1;
				*buffer_index = i;
				ret = 0;
				break;
			}
		spin_unlock(slargs->slot_lock);

		/* if we acquired a buffer, then break out of while */
		if (ret == 0)
			break;

		if (!signal_pending(current)) {
			int timeout =
			    MSECS_TO_JIFFIES(1000 * slot_timeout_secs);
			gossip_debug(GOSSIP_BUFMAP_DEBUG,
				     "[BUFMAP]: waiting %d "
				     "seconds for a slot\n",
				     slot_timeout_secs);
			if (!schedule_timeout(timeout)) {
				gossip_debug(GOSSIP_BUFMAP_DEBUG,
					     "*** wait_for_a_slot timed out\n");
				ret = -ETIMEDOUT;
				break;
			}
			gossip_debug(GOSSIP_BUFMAP_DEBUG,
			  "[BUFMAP]: woken up by a slot becoming available.\n");
			continue;
		}

		gossip_debug(GOSSIP_BUFMAP_DEBUG, "pvfs2: %s interrupted.\n",
			     __func__);
		ret = -EINTR;
		break;
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(slargs->slot_wq, &my_wait);
	return ret;
}

static void put_back_slot(struct slot_args *slargs, int buffer_index)
{
	/* slot_lock is the appropriate index_lock */
	spin_lock(slargs->slot_lock);
	if (buffer_index < 0 || buffer_index >= slargs->slot_count) {
		spin_unlock(slargs->slot_lock);
		return;
	}

	/* put the desc back on the queue */
	slargs->slot_array[buffer_index] = 0;
	spin_unlock(slargs->slot_lock);

	/* wake up anyone who may be sleeping on the queue */
	wake_up_interruptible(slargs->slot_wq);
}

/*
 * pvfs_bufmap_get()
 *
 * gets a free mapped buffer descriptor, will sleep until one becomes
 * available if necessary
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_get(struct pvfs2_bufmap **mapp, int *buffer_index)
{
	struct pvfs2_bufmap *bufmap = pvfs2_bufmap_ref();
	struct slot_args slargs;
	int ret;

	if (!bufmap) {
		gossip_err("pvfs2: please confirm that pvfs2-client daemon is running.\n");
		return -EIO;
	}

	slargs.slot_count = bufmap->desc_count;
	slargs.slot_array = bufmap->buffer_index_array;
	slargs.slot_lock = &bufmap->buffer_index_lock;
	slargs.slot_wq = &bufmap_waitq;
	ret = wait_for_a_slot(&slargs, buffer_index);
	if (ret)
		pvfs2_bufmap_unref(bufmap);
	*mapp = bufmap;
	return ret;
}

/*
 * pvfs_bufmap_put()
 *
 * returns a mapped buffer descriptor to the collection
 *
 * no return value
 */
void pvfs_bufmap_put(struct pvfs2_bufmap *bufmap, int buffer_index)
{
	struct slot_args slargs;

	slargs.slot_count = bufmap->desc_count;
	slargs.slot_array = bufmap->buffer_index_array;
	slargs.slot_lock = &bufmap->buffer_index_lock;
	slargs.slot_wq = &bufmap_waitq;
	put_back_slot(&slargs, buffer_index);
	pvfs2_bufmap_unref(bufmap);
}

/*
 * readdir_index_get()
 *
 * gets a free descriptor, will sleep until one becomes
 * available if necessary.
 * Although the readdir buffers are not mapped into kernel space
 * we could do that at a later point of time. Regardless, these
 * indices are used by the client-core.
 *
 * returns 0 on success, -errno on failure
 */
int readdir_index_get(struct pvfs2_bufmap **mapp, int *buffer_index)
{
	struct pvfs2_bufmap *bufmap = pvfs2_bufmap_ref();
	struct slot_args slargs;
	int ret;

	if (!bufmap) {
		gossip_err("pvfs2: please confirm that pvfs2-client daemon is running.\n");
		return -EIO;
	}

	slargs.slot_count = PVFS2_READDIR_DEFAULT_DESC_COUNT;
	slargs.slot_array = bufmap->readdir_index_array;
	slargs.slot_lock = &bufmap->readdir_index_lock;
	slargs.slot_wq = &readdir_waitq;
	ret = wait_for_a_slot(&slargs, buffer_index);
	if (ret)
		pvfs2_bufmap_unref(bufmap);
	*mapp = bufmap;
	return ret;
}

void readdir_index_put(struct pvfs2_bufmap *bufmap, int buffer_index)
{
	struct slot_args slargs;

	slargs.slot_count = PVFS2_READDIR_DEFAULT_DESC_COUNT;
	slargs.slot_array = bufmap->readdir_index_array;
	slargs.slot_lock = &bufmap->readdir_index_lock;
	slargs.slot_wq = &readdir_waitq;
	put_back_slot(&slargs, buffer_index);
	pvfs2_bufmap_unref(bufmap);
}

/*
 * pvfs_bufmap_copy_iovec_from_user()
 *
 * copies data from several user space address's in an iovec
 * to a mapped buffer
 *
 * Note that the mapped buffer is a series of pages and therefore
 * the copies have to be split by PAGE_SIZE bytes at a time.
 * Note that this routine checks that summation of iov_len
 * across all the elements of iov is equal to size.
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_iovec_from_user(struct pvfs2_bufmap *bufmap,
				     int buffer_index,
				     const struct iovec *iov,
				     unsigned long nr_segs,
				     size_t size)
{
	size_t ret = 0;
	size_t amt_copied = 0;
	size_t cur_copy_size = 0;
	unsigned int to_page_offset = 0;
	unsigned int to_page_index = 0;
	void *to_kaddr = NULL;
	void __user *from_addr = NULL;
	struct iovec *copied_iovec = NULL;
	struct pvfs_bufmap_desc *to;
	unsigned int seg;
	char *tmp_printer = NULL;
	int tmp_int = 0;

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "pvfs_bufmap_copy_iovec_from_user: index %d, "
		     "size %zd\n",
		     buffer_index,
		     size);

	to = &bufmap->desc_array[buffer_index];

	/*
	 * copy the passed in iovec so that we can change some of its fields
	 */
	copied_iovec = kmalloc_array(nr_segs,
				     sizeof(*copied_iovec),
				     PVFS2_BUFMAP_GFP_FLAGS);
	if (copied_iovec == NULL)
		return -ENOMEM;

	memcpy(copied_iovec, iov, nr_segs * sizeof(*copied_iovec));
	/*
	 * Go through each segment in the iovec and make sure that
	 * the summation of iov_len matches the given size.
	 */
	for (seg = 0, amt_copied = 0; seg < nr_segs; seg++)
		amt_copied += copied_iovec[seg].iov_len;
	if (amt_copied != size) {
		gossip_err(
		    "pvfs2_bufmap_copy_iovec_from_user: computed total ("
		    "%zd) is not equal to (%zd)\n",
		    amt_copied,
		    size);
		kfree(copied_iovec);
		return -EINVAL;
	}

	to_page_index = 0;
	to_page_offset = 0;
	amt_copied = 0;
	seg = 0;
	/*
	 * Go through each segment in the iovec and copy its
	 * buffer into the mapped buffer one page at a time though
	 */
	while (amt_copied < size) {
		struct iovec *iv = &copied_iovec[seg];
		int inc_to_page_index;

		if (iv->iov_len < (PAGE_SIZE - to_page_offset)) {
			cur_copy_size =
			    PVFS_util_min(iv->iov_len, size - amt_copied);
			seg++;
			from_addr = iv->iov_base;
			inc_to_page_index = 0;
		} else if (iv->iov_len == (PAGE_SIZE - to_page_offset)) {
			cur_copy_size =
			    PVFS_util_min(iv->iov_len, size - amt_copied);
			seg++;
			from_addr = iv->iov_base;
			inc_to_page_index = 1;
		} else {
			cur_copy_size =
			    PVFS_util_min(PAGE_SIZE - to_page_offset,
					  size - amt_copied);
			from_addr = iv->iov_base;
			iv->iov_base += cur_copy_size;
			iv->iov_len -= cur_copy_size;
			inc_to_page_index = 1;
		}
		to_kaddr = pvfs2_kmap(to->page_array[to_page_index]);
		ret =
		    copy_from_user(to_kaddr + to_page_offset,
				   from_addr,
				   cur_copy_size);
		if (!PageReserved(to->page_array[to_page_index]))
			SetPageDirty(to->page_array[to_page_index]);

		if (!tmp_printer) {
			tmp_printer = (char *)(to_kaddr + to_page_offset);
			tmp_int += tmp_printer[0];
			gossip_debug(GOSSIP_BUFMAP_DEBUG,
				     "First character (integer value) in pvfs_bufmap_copy_from_user: %d\n",
				     tmp_int);
		}

		pvfs2_kunmap(to->page_array[to_page_index]);
		if (ret) {
			gossip_err("Failed to copy data from user space\n");
			kfree(copied_iovec);
			return -EFAULT;
		}

		amt_copied += cur_copy_size;
		if (inc_to_page_index) {
			to_page_offset = 0;
			to_page_index++;
		} else {
			to_page_offset += cur_copy_size;
		}
	}
	kfree(copied_iovec);
	return 0;
}

/*
 * pvfs_bufmap_copy_iovec_from_kernel()
 *
 * copies data from several kernel space address's in an iovec
 * to a mapped buffer
 *
 * Note that the mapped buffer is a series of pages and therefore
 * the copies have to be split by PAGE_SIZE bytes at a time.
 * Note that this routine checks that summation of iov_len
 * across all the elements of iov is equal to size.
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_iovec_from_kernel(struct pvfs2_bufmap *bufmap,
		int buffer_index, const struct iovec *iov,
		unsigned long nr_segs, size_t size)
{
	size_t amt_copied = 0;
	size_t cur_copy_size = 0;
	int to_page_index = 0;
	void *to_kaddr = NULL;
	void *from_kaddr = NULL;
	struct iovec *copied_iovec = NULL;
	struct pvfs_bufmap_desc *to;
	unsigned int seg;
	unsigned to_page_offset = 0;

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "pvfs_bufmap_copy_iovec_from_kernel: index %d, "
		     "size %zd\n",
		     buffer_index,
		     size);

	to = &bufmap->desc_array[buffer_index];
	/*
	 * copy the passed in iovec so that we can change some of its fields
	 */
	copied_iovec = kmalloc_array(nr_segs,
				     sizeof(*copied_iovec),
				     PVFS2_BUFMAP_GFP_FLAGS);
	if (copied_iovec == NULL)
		return -ENOMEM;

	memcpy(copied_iovec, iov, nr_segs * sizeof(*copied_iovec));
	/*
	 * Go through each segment in the iovec and make sure that
	 * the summation of iov_len matches the given size.
	 */
	for (seg = 0, amt_copied = 0; seg < nr_segs; seg++)
		amt_copied += copied_iovec[seg].iov_len;
	if (amt_copied != size) {
		gossip_err("pvfs2_bufmap_copy_iovec_from_kernel: computed total(%zd) is not equal to (%zd)\n",
			   amt_copied,
			   size);
		kfree(copied_iovec);
		return -EINVAL;
	}

	to_page_index = 0;
	amt_copied = 0;
	seg = 0;
	to_page_offset = 0;
	/*
	 * Go through each segment in the iovec and copy its
	 * buffer into the mapped buffer one page at a time though
	 */
	while (amt_copied < size) {
		struct iovec *iv = &copied_iovec[seg];
		int inc_to_page_index;

		if (iv->iov_len < (PAGE_SIZE - to_page_offset)) {
			cur_copy_size =
			    PVFS_util_min(iv->iov_len, size - amt_copied);
			seg++;
			from_kaddr = iv->iov_base;
			inc_to_page_index = 0;
		} else if (iv->iov_len == (PAGE_SIZE - to_page_offset)) {
			cur_copy_size =
			    PVFS_util_min(iv->iov_len, size - amt_copied);
			seg++;
			from_kaddr = iv->iov_base;
			inc_to_page_index = 1;
		} else {
			cur_copy_size =
			    PVFS_util_min(PAGE_SIZE - to_page_offset,
					  size - amt_copied);
			from_kaddr = iv->iov_base;
			iv->iov_base += cur_copy_size;
			iv->iov_len -= cur_copy_size;
			inc_to_page_index = 1;
		}
		to_kaddr = pvfs2_kmap(to->page_array[to_page_index]);
		memcpy(to_kaddr + to_page_offset, from_kaddr, cur_copy_size);
		if (!PageReserved(to->page_array[to_page_index]))
			SetPageDirty(to->page_array[to_page_index]);
		pvfs2_kunmap(to->page_array[to_page_index]);
		amt_copied += cur_copy_size;
		if (inc_to_page_index) {
			to_page_offset = 0;
			to_page_index++;
		} else {
			to_page_offset += cur_copy_size;
		}
	}
	kfree(copied_iovec);
	return 0;
}

/*
 * pvfs_bufmap_copy_to_user_iovec()
 *
 * copies data to several user space address's in an iovec
 * from a mapped buffer
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_to_user_iovec(struct pvfs2_bufmap *bufmap,
		int buffer_index, const struct iovec *iov,
		unsigned long nr_segs, size_t size)
{
	size_t ret = 0;
	size_t amt_copied = 0;
	size_t cur_copy_size = 0;
	int from_page_index = 0;
	void *from_kaddr = NULL;
	void __user *to_addr = NULL;
	struct iovec *copied_iovec = NULL;
	struct pvfs_bufmap_desc *from;
	unsigned int seg;
	unsigned from_page_offset = 0;
	char *tmp_printer = NULL;
	int tmp_int = 0;

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "pvfs_bufmap_copy_to_user_iovec: index %d, size %zd\n",
		     buffer_index,
		     size);

	from = &bufmap->desc_array[buffer_index];
	/*
	 * copy the passed in iovec so that we can change some of its fields
	 */
	copied_iovec = kmalloc_array(nr_segs,
				     sizeof(*copied_iovec),
				     PVFS2_BUFMAP_GFP_FLAGS);
	if (copied_iovec == NULL)
		return -ENOMEM;

	memcpy(copied_iovec, iov, nr_segs * sizeof(*copied_iovec));
	/*
	 * Go through each segment in the iovec and make sure that
	 * the summation of iov_len is greater than the given size.
	 */
	for (seg = 0, amt_copied = 0; seg < nr_segs; seg++)
		amt_copied += copied_iovec[seg].iov_len;
	if (amt_copied < size) {
		gossip_err("pvfs2_bufmap_copy_to_user_iovec: computed total (%zd) is less than (%zd)\n",
			   amt_copied,
			   size);
		kfree(copied_iovec);
		return -EINVAL;
	}

	from_page_index = 0;
	amt_copied = 0;
	seg = 0;
	from_page_offset = 0;
	/*
	 * Go through each segment in the iovec and copy from the mapper buffer,
	 * but make sure that we do so one page at a time.
	 */
	while (amt_copied < size) {
		struct iovec *iv = &copied_iovec[seg];
		int inc_from_page_index;

		if (iv->iov_len < (PAGE_SIZE - from_page_offset)) {
			cur_copy_size =
			    PVFS_util_min(iv->iov_len, size - amt_copied);
			seg++;
			to_addr = iv->iov_base;
			inc_from_page_index = 0;
		} else if (iv->iov_len == (PAGE_SIZE - from_page_offset)) {
			cur_copy_size =
			    PVFS_util_min(iv->iov_len, size - amt_copied);
			seg++;
			to_addr = iv->iov_base;
			inc_from_page_index = 1;
		} else {
			cur_copy_size =
			    PVFS_util_min(PAGE_SIZE - from_page_offset,
					  size - amt_copied);
			to_addr = iv->iov_base;
			iv->iov_base += cur_copy_size;
			iv->iov_len -= cur_copy_size;
			inc_from_page_index = 1;
		}
		from_kaddr = pvfs2_kmap(from->page_array[from_page_index]);
		if (!tmp_printer) {
			tmp_printer = (char *)(from_kaddr + from_page_offset);
			tmp_int += tmp_printer[0];
			gossip_debug(GOSSIP_BUFMAP_DEBUG,
				     "First character (integer value) in pvfs_bufmap_copy_to_user_iovec: %d\n",
				     tmp_int);
		}
		ret =
		    copy_to_user(to_addr,
				 from_kaddr + from_page_offset,
				 cur_copy_size);
		pvfs2_kunmap(from->page_array[from_page_index]);
		if (ret) {
			gossip_err("Failed to copy data to user space\n");
			kfree(copied_iovec);
			return -EFAULT;
		}

		amt_copied += cur_copy_size;
		if (inc_from_page_index) {
			from_page_offset = 0;
			from_page_index++;
		} else {
			from_page_offset += cur_copy_size;
		}
	}
	kfree(copied_iovec);
	return 0;
}

/*
 * pvfs_bufmap_copy_to_kernel_iovec()
 *
 * copies data to several kernel space address's in an iovec
 * from a mapped buffer
 *
 * returns 0 on success, -errno on failure
 */
int pvfs_bufmap_copy_to_kernel_iovec(struct pvfs2_bufmap *bufmap,
		int buffer_index, const struct iovec *iov,
		unsigned long nr_segs, size_t size)
{
	size_t amt_copied = 0;
	size_t cur_copy_size = 0;
	int from_page_index = 0;
	void *from_kaddr = NULL;
	void *to_kaddr = NULL;
	struct iovec *copied_iovec = NULL;
	struct pvfs_bufmap_desc *from;
	unsigned int seg;
	unsigned int from_page_offset = 0;

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "pvfs_bufmap_copy_to_kernel_iovec: index %d, size %zd\n",
		      buffer_index,
		      size);

	from = &bufmap->desc_array[buffer_index];
	/*
	 * copy the passed in iovec so that we can change some of its fields
	 */
	copied_iovec = kmalloc_array(nr_segs,
				     sizeof(*copied_iovec),
				     PVFS2_BUFMAP_GFP_FLAGS);
	if (copied_iovec == NULL)
		return -ENOMEM;

	memcpy(copied_iovec, iov, nr_segs * sizeof(*copied_iovec));
	/*
	 * Go through each segment in the iovec and make sure that
	 * the summation of iov_len is greater than the given size.
	 */
	for (seg = 0, amt_copied = 0; seg < nr_segs; seg++)
		amt_copied += copied_iovec[seg].iov_len;

	if (amt_copied < size) {
		gossip_err("pvfs2_bufmap_copy_to_kernel_iovec: computed total (%zd) is less than (%zd)\n",
		     amt_copied,
		     size);
		kfree(copied_iovec);
		return -EINVAL;
	}

	from_page_index = 0;
	amt_copied = 0;
	seg = 0;
	from_page_offset = 0;
	/*
	 * Go through each segment in the iovec and copy from the mapper buffer,
	 * but make sure that we do so one page at a time.
	 */
	while (amt_copied < size) {
		struct iovec *iv = &copied_iovec[seg];
		int inc_from_page_index;

		if (iv->iov_len < (PAGE_SIZE - from_page_offset)) {
			cur_copy_size =
			    PVFS_util_min(iv->iov_len, size - amt_copied);
			seg++;
			to_kaddr = iv->iov_base;
			inc_from_page_index = 0;
		} else if (iv->iov_len == (PAGE_SIZE - from_page_offset)) {
			cur_copy_size =
			    PVFS_util_min(iv->iov_len, size - amt_copied);
			seg++;
			to_kaddr = iv->iov_base;
			inc_from_page_index = 1;
		} else {
			cur_copy_size =
			    PVFS_util_min(PAGE_SIZE - from_page_offset,
					  size - amt_copied);
			to_kaddr = iv->iov_base;
			iv->iov_base += cur_copy_size;
			iv->iov_len -= cur_copy_size;
			inc_from_page_index = 1;
		}
		from_kaddr = pvfs2_kmap(from->page_array[from_page_index]);
		memcpy(to_kaddr, from_kaddr + from_page_offset, cur_copy_size);
		pvfs2_kunmap(from->page_array[from_page_index]);
		amt_copied += cur_copy_size;
		if (inc_from_page_index) {
			from_page_offset = 0;
			from_page_index++;
		} else {
			from_page_offset += cur_copy_size;
		}
	}
	kfree(copied_iovec);
	return 0;
}
